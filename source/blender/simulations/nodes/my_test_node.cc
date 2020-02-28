#include <cstring>
#include <typeinfo>

#include "BKE_node.h"
#include "BKE_virtual_node_tree.h"

#include "SIM_node_tree.h"

#include "BLI_vector.h"
#include "BLI_string_ref.h"
#include "BLI_set.h"
#include "BLI_linear_allocator.h"
#include "BLI_color.h"
#include "BLI_string.h"
#include "BLI_array_cxx.h"
#include "BLI_map.h"
#include "BLI_listbase_wrapper.h"
#include "BLI_string_utils.h"
#include "BLI_linear_allocated_vector.h"
#include "BLI_index_mask.h"

#include "PIL_time.h"

#include "BKE_context.h"

#include "DNA_space_types.h"

#include "UI_interface.h"

#include "../space_node/node_intern.h"

using BLI::Array;
using BLI::ArrayRef;
using BLI::IndexMask;
using BLI::IntrusiveListBaseWrapper;
using BLI::LinearAllocatedVector;
using BLI::LinearAllocator;
using BLI::Map;
using BLI::MutableArrayRef;
using BLI::rgba_f;
using BLI::Set;
using BLI::StringRef;
using BLI::StringRefNull;
using BLI::Vector;

class SocketDataType;
class BaseSocketDataType;
class ListSocketDataType;

enum class SocketTypeCategory {
  Base,
  List,
};

class SocketDataType {
 public:
  std::string m_ui_name;
  bNodeSocketType *m_socket_type;
  SocketTypeCategory m_category;

  SocketDataType(StringRef ui_name, bNodeSocketType *socket_type, SocketTypeCategory category)
      : m_ui_name(ui_name), m_socket_type(socket_type), m_category(category)
  {
  }
};

class BaseSocketDataType : public SocketDataType {
 public:
  ListSocketDataType *m_list_type;

  BaseSocketDataType(StringRef ui_name, bNodeSocketType *socket_type)
      : SocketDataType(ui_name, socket_type, SocketTypeCategory::Base)
  {
  }
};

class ListSocketDataType : public SocketDataType {
 public:
  BaseSocketDataType *m_base_type;

  ListSocketDataType(StringRef ui_name, bNodeSocketType *socket_type)
      : SocketDataType(ui_name, socket_type, SocketTypeCategory::List)
  {
  }
};

class DataTypesInfo {
 private:
  Set<SocketDataType *> m_data_types;

 public:
  void add_data_type(SocketDataType *data_type)
  {
    m_data_types.add_new(data_type);
  }
};

static DataTypesInfo *socket_data_types;

static BaseSocketDataType *data_socket_float;
static BaseSocketDataType *data_socket_int;
static ListSocketDataType *data_socket_float_list;
static ListSocketDataType *data_socket_int_list;

enum class SocketDeclCategory {
  Mockup,
  FixedDataType,
  Operator,
};

class SocketDecl {
 private:
  bNodeSocketType *m_current_type;
  StringRefNull m_identifier;
  StringRefNull m_ui_name;
  SocketDeclCategory m_category;

 protected:
  SocketDecl(bNodeSocketType *current_type,
             StringRefNull identifier,
             StringRefNull ui_name,
             SocketDeclCategory category)
      : m_current_type(current_type),
        m_identifier(identifier),
        m_ui_name(ui_name),
        m_category(category)
  {
  }

 public:
  void build(bNodeTree *ntree, bNode *node, eNodeSocketInOut in_out)
  {
    nodeAddSocket(
        ntree, node, in_out, m_current_type->idname, m_identifier.data(), m_ui_name.data());
  }

  SocketDeclCategory category() const
  {
    return m_category;
  }

  bNodeSocketType *current_type() const
  {
    return m_current_type;
  }

  StringRefNull identifier() const
  {
    return m_identifier;
  }

  StringRefNull ui_name() const
  {
    return m_ui_name;
  }

  bool socket_is_correct(const bNodeSocket *socket)
  {
    BLI_assert(socket != nullptr);
    if (socket->typeinfo != m_current_type) {
      return false;
    }
    if (socket->name != m_ui_name) {
      return false;
    }
    if (socket->identifier != m_identifier) {
      return false;
    }
    return true;
  }
};

using OperatorSocketFn = void (*)(bNodeTree *ntree,
                                  bNode *node,
                                  bNodeSocket *socket,
                                  bNodeSocket *directly_linked_socket,
                                  bNodeSocket *linked_socket);

class OperatorSocketDecl final : public SocketDecl {
 private:
  OperatorSocketFn m_callback;

 public:
  OperatorSocketDecl(StringRefNull identifier, StringRefNull ui_name, OperatorSocketFn callback)
      : SocketDecl(nodeSocketTypeFind("OperatorSocket"),
                   identifier,
                   ui_name,
                   SocketDeclCategory::Operator),
        m_callback(callback)
  {
  }

  const OperatorSocketFn callback() const
  {
    return m_callback;
  }
};

class FixedDataTypeSocketDecl final : public SocketDecl {
 private:
  const SocketDataType *m_data_type;

 public:
  FixedDataTypeSocketDecl(StringRefNull identifier,
                          StringRefNull ui_name,
                          const SocketDataType *data_type)
      : SocketDecl(
            data_type->m_socket_type, identifier, ui_name, SocketDeclCategory::FixedDataType),
        m_data_type(data_type)
  {
  }

  const SocketDataType *data_type() const
  {
    return m_data_type;
  }
};

class MockupSocketDecl final : public SocketDecl {
 public:
  MockupSocketDecl(bNodeSocketType *type, StringRefNull identifier, StringRefNull ui_name)
      : SocketDecl(type, identifier, ui_name, SocketDeclCategory::Mockup)
  {
  }
};

class NodeDecl {
 public:
  bNodeTree *m_ntree;
  bNode *m_node;
  LinearAllocatedVector<SocketDecl *> m_inputs;
  LinearAllocatedVector<SocketDecl *> m_outputs;
  bool m_has_operator_input = false;

  NodeDecl(bNodeTree *ntree, bNode *node) : m_ntree(ntree), m_node(node)
  {
  }

  void reserve_decls(LinearAllocator<> &allocator, uint input_amount, uint output_amount)
  {
    m_inputs.reserve(input_amount, allocator);
    m_outputs.reserve(output_amount, allocator);
  }

  void build() const
  {
    nodeRemoveAllSockets(m_ntree, m_node);
    for (SocketDecl *decl : m_inputs) {
      decl->build(m_ntree, m_node, SOCK_IN);
    }
    for (SocketDecl *decl : m_outputs) {
      decl->build(m_ntree, m_node, SOCK_OUT);
    }
  }

  bool sockets_are_correct() const
  {
    if (!this->sockets_are_correct(m_node->inputs, m_inputs)) {
      return false;
    }
    if (!this->sockets_are_correct(m_node->outputs, m_outputs)) {
      return false;
    }
    return true;
  }

 private:
  bool sockets_are_correct(ListBase &sockets_list, ArrayRef<SocketDecl *> decls) const
  {
    uint i = 0;
    LISTBASE_FOREACH (bNodeSocket *, socket, &sockets_list) {
      if (i == decls.size()) {
        return false;
      }
      SocketDecl *decl = decls[i];
      if (!decl->socket_is_correct(socket)) {
        return false;
      }
      i++;
    }
    if (i != decls.size()) {
      return false;
    }
    return true;
  }
};

template<typename T> static T *get_node_storage(bNode *node);
template<typename T> static const T *get_node_storage(const bNode *node);
template<typename T> static T *get_socket_storage(bNodeSocket *socket);

class NodeBuilder {
 private:
  LinearAllocator<> &m_allocator;
  NodeDecl &m_node_decl;

 public:
  NodeBuilder(LinearAllocator<> &allocator, NodeDecl &node_decl)
      : m_allocator(allocator), m_node_decl(node_decl)
  {
  }

  template<typename T> T *node_storage()
  {
    return get_node_storage<T>(m_node_decl.m_node);
  }

  void fixed_input(StringRef identifier, StringRef ui_name, SocketDataType &type)
  {
    FixedDataTypeSocketDecl *decl = m_allocator.construct<FixedDataTypeSocketDecl>(
        m_allocator.copy_string(identifier), m_allocator.copy_string(ui_name), &type);
    m_node_decl.m_inputs.append(decl, m_allocator);
  }

  void fixed_output(StringRef identifier, StringRef ui_name, SocketDataType &type)
  {
    FixedDataTypeSocketDecl *decl = m_allocator.construct<FixedDataTypeSocketDecl>(
        m_allocator.copy_string(identifier), m_allocator.copy_string(ui_name), &type);
    m_node_decl.m_outputs.append(decl, m_allocator);
  }

  void operator_input(StringRef identifier, StringRef ui_name, OperatorSocketFn callback)
  {
    OperatorSocketDecl *decl = m_allocator.construct<OperatorSocketDecl>(
        m_allocator.copy_string(ui_name), m_allocator.copy_string(identifier), callback);
    m_node_decl.m_inputs.append(decl, m_allocator);
    m_node_decl.m_has_operator_input = true;
  }

  void float_input(StringRef identifier, StringRef ui_name)
  {
    this->fixed_input(identifier, ui_name, *data_socket_float);
  }

  void int_input(StringRef identifier, StringRef ui_name)
  {
    this->fixed_input(identifier, ui_name, *data_socket_int);
  }

  void float_output(StringRef identifier, StringRef ui_name)
  {
    this->fixed_output(identifier, ui_name, *data_socket_float);
  }

  void int_output(StringRef identifier, StringRef ui_name)
  {
    this->fixed_output(identifier, ui_name, *data_socket_int);
  }
};

static void declare_test_node(NodeBuilder &builder)
{
  MyTestNodeStorage *storage = builder.node_storage<MyTestNodeStorage>();

  builder.float_input("a", "ID 1");
  builder.int_input("b", "ID 2");
  builder.int_input("c", "ID 4");
  builder.float_output("c", "ID 3");

  for (int i = 0; i < storage->x; i++) {
    builder.fixed_input(
        "id" + std::to_string(i), "Hello " + std::to_string(i), *data_socket_float_list);
  }
}

class SocketDefinition {
 public:
  using DrawInNodeFn = std::function<void(struct bContext *C,
                                          struct uiLayout *layout,
                                          struct PointerRNA *ptr,
                                          struct PointerRNA *node_ptr,
                                          const char *text)>;
  using NewStorageFn = std::function<void *()>;
  using CopyStorageFn = std::function<void *(const void *)>;
  using FreeStorageFn = std::function<void(void *)>;
  template<typename T> using TypedInitStorageFn = std::function<void(T *)>;

 private:
  bNodeSocketType m_stype;
  DrawInNodeFn m_draw_in_node_fn;
  rgba_f m_color = {0.0f, 0.0f, 0.0f, 1.0f};
  std::string m_storage_struct_name;
  NewStorageFn m_new_storage_fn;
  CopyStorageFn m_copy_storage_fn;
  FreeStorageFn m_free_storage_fn;

 public:
  SocketDefinition(StringRef idname)
  {
    memset(&m_stype, 0, sizeof(bNodeSocketType));
    idname.copy(m_stype.idname);
    m_stype.type = SOCK_CUSTOM;
    m_stype.draw = SocketDefinition::draw_in_node;
    m_draw_in_node_fn = [](struct bContext *UNUSED(C),
                           struct uiLayout *layout,
                           struct PointerRNA *UNUSED(ptr),
                           struct PointerRNA *UNUSED(node_ptr),
                           const char *text) { uiItemL(layout, text, 0); };

    m_stype.draw_color = SocketDefinition::get_draw_color;
    m_stype.free_self = [](bNodeSocketType *UNUSED(stype)) {};

    m_new_storage_fn = []() { return nullptr; };
    m_copy_storage_fn = [](const void *storage) {
      BLI_assert(storage == nullptr);
      UNUSED_VARS_NDEBUG(storage);
      return nullptr;
    };
    m_free_storage_fn = [](void *storage) {
      BLI_assert(storage == nullptr);
      UNUSED_VARS_NDEBUG(storage);
    };

    m_stype.init_fn = SocketDefinition::init_socket;
    m_stype.copy_fn = SocketDefinition::copy_socket;
    m_stype.free_fn = SocketDefinition::free_socket;

    m_stype.userdata = (void *)this;
  }

  void set_color(rgba_f color)
  {
    m_color = color;
  }

  void add_dna_storage(StringRef struct_name,
                       NewStorageFn init_storage_fn,
                       CopyStorageFn copy_storage_fn,
                       FreeStorageFn free_storage_fn)
  {
    m_storage_struct_name = struct_name;
    m_new_storage_fn = init_storage_fn;
    m_copy_storage_fn = copy_storage_fn;
    m_free_storage_fn = free_storage_fn;
  }

  template<typename T>
  void add_dna_storage(StringRef struct_name, TypedInitStorageFn<T> init_storage_fn)
  {
    this->add_dna_storage(
        struct_name,
        [init_storage_fn]() {
          void *buffer = MEM_callocN(sizeof(T), __func__);
          init_storage_fn((T *)buffer);
          return buffer;
        },
        [](const void *buffer) {
          void *new_buffer = MEM_callocN(sizeof(T), __func__);
          memcpy(new_buffer, buffer, sizeof(T));
          return new_buffer;
        },
        [](void *buffer) { MEM_freeN(buffer); });
  }

  void add_draw_fn(DrawInNodeFn draw_in_node_fn)
  {
    m_draw_in_node_fn = draw_in_node_fn;
  }

  StringRefNull storage_struct_name() const
  {
    return m_storage_struct_name;
  }

  void register_type()
  {
    nodeRegisterSocketType(&m_stype);
  }

  static const SocketDefinition *get_from_socket(bNodeSocket *socket)
  {
    const SocketDefinition *def = (const SocketDefinition *)socket->typeinfo->userdata;
    BLI_assert(def != nullptr);
    return def;
  }

  void *get_dna_storage_copy(bNodeSocket *socket) const
  {
    if (socket->default_value == nullptr) {
      return nullptr;
    }
    void *storage_copy = m_copy_storage_fn(socket->default_value);
    return storage_copy;
  }

  void free_dna_storage(void *storage) const
  {
    m_free_storage_fn(storage);
  }

 private:
  static void init_socket(bNodeTree *UNUSED(ntree), bNode *UNUSED(node), bNodeSocket *socket)
  {
    const SocketDefinition *def = get_from_socket(socket);
    socket->default_value = def->m_new_storage_fn();
  }

  static void copy_socket(bNodeTree *UNUSED(dst_ntree),
                          bNode *UNUSED(dst_node),
                          bNodeSocket *dst_socket,
                          const bNodeSocket *src_socket)
  {
    const SocketDefinition *def = get_from_socket(dst_socket);
    dst_socket->default_value = def->m_copy_storage_fn(src_socket->default_value);
  }

  static void free_socket(bNodeTree *UNUSED(ntree), bNode *UNUSED(node), bNodeSocket *socket)
  {
    const SocketDefinition *def = get_from_socket(socket);
    def->m_free_storage_fn(socket->default_value);
    socket->default_value = nullptr;
  }

  static void draw_in_node(struct bContext *C,
                           struct uiLayout *layout,
                           struct PointerRNA *ptr,
                           struct PointerRNA *node_ptr,
                           const char *text)
  {
    bNodeSocket *socket = (bNodeSocket *)ptr->data;
    const SocketDefinition *def = get_from_socket(socket);
    def->m_draw_in_node_fn(C, layout, ptr, node_ptr, text);
  }

  static void get_draw_color(struct bContext *UNUSED(C),
                             struct PointerRNA *ptr,
                             struct PointerRNA *UNUSED(node_ptr),
                             float *r_color)
  {
    bNodeSocket *socket = (bNodeSocket *)ptr->data;
    const SocketDefinition *def = get_from_socket(socket);
    memcpy(r_color, def->m_color, sizeof(rgba_f));
  }
};

class NodeDefinition {
 public:
  using DeclareNodeFn = std::function<void(NodeBuilder &node_builder)>;
  using NewStorageFn = std::function<void *()>;
  using CopyStorageFn = std::function<void *(const void *)>;
  using FreeStorageFn = std::function<void(void *)>;
  using DrawInNodeFn =
      std::function<void(struct uiLayout *layout, struct bContext *C, struct PointerRNA *ptr)>;
  template<typename T> using TypedNewStorageFn = std::function<T *()>;
  template<typename T> using TypedCopyStorageFn = std::function<T *(const T *)>;
  template<typename T> using TypedFreeStorageFn = std::function<void(T *)>;
  template<typename T> using TypedInitStorageFn = std::function<void(T *)>;
  using CopyBehaviorFn = std::function<void(bNode *dst_node, const bNode *src_node)>;
  using LabelFn = std::function<void(bNodeTree *ntree, bNode *node, char *r_label, int maxlen)>;

 private:
  bNodeType m_ntype;
  DeclareNodeFn m_declare_node_fn;
  NewStorageFn m_new_storage_fn;
  CopyStorageFn m_copy_storage_fn;
  FreeStorageFn m_free_storage_fn;
  CopyBehaviorFn m_copy_node_fn;
  DrawInNodeFn m_draw_in_node_fn;
  LabelFn m_label_fn;

 public:
  NodeDefinition(StringRef idname, StringRef ui_name, StringRef ui_description)
  {
    bNodeType *ntype = &m_ntype;

    memset(ntype, 0, sizeof(bNodeType));
    ntype->minwidth = 20;
    ntype->minheight = 20;
    ntype->maxwidth = 1000;
    ntype->maxheight = 1000;
    ntype->height = 100;
    ntype->width = 140;
    ntype->type = NODE_CUSTOM;

    idname.copy(ntype->idname);
    ui_name.copy(ntype->ui_name);
    ui_description.copy(ntype->ui_description);

    ntype->userdata = (void *)this;

    m_declare_node_fn = [](NodeBuilder &UNUSED(builder)) {};
    m_new_storage_fn = []() { return nullptr; };
    m_copy_storage_fn = [](const void *storage) {
      BLI_assert(storage == nullptr);
      UNUSED_VARS_NDEBUG(storage);
      return nullptr;
    };
    m_free_storage_fn = [](void *storage) {
      BLI_assert(storage == nullptr);
      UNUSED_VARS_NDEBUG(storage);
    };
    m_draw_in_node_fn = [](struct uiLayout *UNUSED(layout),
                           struct bContext *UNUSED(C),
                           struct PointerRNA *UNUSED(ptr)) {};
    m_copy_node_fn = [](bNode *UNUSED(dst_node), const bNode *UNUSED(src_node)) {};

    ntype->poll = [](bNodeType *UNUSED(ntype), bNodeTree *UNUSED(ntree)) { return true; };
    ntype->initfunc = init_node;
    ntype->copyfunc = copy_node;
    ntype->freefunc = free_node;

    ntype->draw_buttons = [](struct uiLayout *layout, struct bContext *C, struct PointerRNA *ptr) {
      bNode *node = (bNode *)ptr->data;
      NodeDefinition *def = type_from_node(node);
      def->m_draw_in_node_fn(layout, C, ptr);
    };

    ntype->draw_nodetype = node_draw_default;
    ntype->draw_nodetype_prepare = node_update_default;
    ntype->select_area_func = node_select_area_default;
    ntype->tweak_area_func = node_tweak_area_default;
    ntype->resize_area_func = node_resize_area_default;
    ntype->draw_buttons_ex = nullptr;
  }

  void add_declaration(DeclareNodeFn declare_fn)
  {
    m_declare_node_fn = declare_fn;
  }

  void add_dna_storage(StringRef struct_name,
                       NewStorageFn new_storage_fn,
                       CopyStorageFn copy_storage_fn,
                       FreeStorageFn free_storage_fn)
  {
    struct_name.copy(m_ntype.storagename);
    m_new_storage_fn = new_storage_fn;
    m_copy_storage_fn = copy_storage_fn;
    m_free_storage_fn = free_storage_fn;
  }

  template<typename T>
  void add_dna_storage(StringRef struct_name,
                       TypedNewStorageFn<T> new_storage_fn,
                       TypedCopyStorageFn<T> copy_storage_fn,
                       TypedFreeStorageFn<T> free_storage_fn)
  {
    this->add_dna_storage(
        struct_name,
        [new_storage_fn]() { return (void *)new_storage_fn(); },
        [copy_storage_fn](const void *storage_) {
          const T *storage = (const T *)storage_;
          T *new_storage = copy_storage_fn(storage);
          return (void *)new_storage;
        },
        [free_storage_fn](const void *storage_) {
          T *storage = (T *)storage_;
          free_storage_fn(storage);
        });
  }

  template<typename T>
  void add_dna_storage(StringRef struct_name, TypedInitStorageFn<T> init_storage_fn)
  {
    this->add_dna_storage(
        struct_name,
        [init_storage_fn]() {
          void *buffer = MEM_callocN(sizeof(T), __func__);
          init_storage_fn((T *)buffer);
          return buffer;
        },
        [](const void *buffer) {
          void *new_buffer = MEM_callocN(sizeof(T), __func__);
          memcpy(new_buffer, buffer, sizeof(T));
          return new_buffer;
        },
        [](void *buffer) { MEM_freeN(buffer); });
  }

  void add_copy_behavior(CopyBehaviorFn copy_fn)
  {
    m_copy_node_fn = copy_fn;
  }

  template<typename T>
  void add_copy_behavior(std::function<void(T *dst_storage, const T *src_storage)> copy_fn)
  {
    this->add_copy_behavior([copy_fn](bNode *dst_node, const bNode *src_node) {
      T *dst_storage = get_node_storage<T>(dst_node);
      const T *src_storage = get_node_storage<T>(src_node);
      copy_fn(dst_storage, src_storage);
    });
  }

  void add_draw_fn(DrawInNodeFn draw_fn)
  {
    m_draw_in_node_fn = draw_fn;
  }

  void add_label_fn(LabelFn label_fn)
  {
    m_ntype.labelfunc = node_label;
    m_label_fn = label_fn;
  }

  void register_type()
  {
    nodeRegisterType(&m_ntype);
  }

  static void declare_node(bNode *node, NodeBuilder &builder)
  {
    NodeDefinition *def = type_from_node(node);
    def->m_declare_node_fn(builder);
  }

 private:
  static NodeDefinition *type_from_node(bNode *node)
  {
    return (NodeDefinition *)node->typeinfo->userdata;
  }

  static void init_node(bNodeTree *ntree, bNode *node)
  {
    NodeDefinition *def = type_from_node(node);

    LinearAllocator<> allocator;
    NodeDecl node_decl{ntree, node};
    NodeBuilder node_builder{allocator, node_decl};
    node->storage = def->m_new_storage_fn();
    def->m_declare_node_fn(node_builder);
    node_decl.build();
  }

  static void copy_node(bNodeTree *UNUSED(dst_ntree), bNode *dst_node, const bNode *src_node)
  {
    BLI_assert(dst_node->typeinfo == src_node->typeinfo);
    NodeDefinition *def = type_from_node(dst_node);

    dst_node->storage = def->m_copy_storage_fn(src_node->storage);
    def->m_copy_node_fn(dst_node, src_node);
  }

  static void free_node(bNode *node)
  {
    NodeDefinition *def = type_from_node(node);
    def->m_free_storage_fn(node->storage);
    node->storage = nullptr;
  }

  static void node_label(bNodeTree *ntree, bNode *node, char *r_label, int maxlen)
  {
    NodeDefinition *def = type_from_node(node);
    def->m_label_fn(ntree, node, r_label, maxlen);
  }
};

template<typename T> static T *get_node_storage(bNode *node)
{
#ifdef DEBUG
  const char *type_name = typeid(T).name();
  const char *expected_name = node->typeinfo->storagename;
  BLI_assert(strstr(type_name, expected_name));
#endif
  return (T *)node->storage;
}

template<typename T> static const T *get_node_storage(const bNode *node)
{
#ifdef DEBUG
  const char *type_name = typeid(T).name();
  const char *expected_name = node->typeinfo->storagename;
  BLI_assert(strstr(type_name, expected_name));
#endif
  return (const T *)node->storage;
}

template<typename T> static T *get_socket_storage(bNodeSocket *socket)
{
#ifdef DEBUG
  const char *type_name = typeid(T).name();
  const char *expected_name =
      SocketDefinition::get_from_socket(socket)->storage_struct_name().data();
  BLI_assert(strstr(type_name, expected_name));
#endif
  return (T *)socket->default_value;
}

static void update_tree(bContext *C)
{
  bNodeTree *ntree = CTX_wm_space_node(C)->edittree;
  ntree->update = NTREE_UPDATE;
  ntreeUpdateTree(CTX_data_main(C), ntree);
}

void register_node_type_my_test_node()
{
  {
    static NodeDefinition ntype("MyTestNode", "My Test Node", "My Description");
    ntype.add_declaration(declare_test_node);
    ntype.add_dna_storage<MyTestNodeStorage>("MyTestNodeStorage",
                                             [](MyTestNodeStorage *storage) { storage->x = 3; });
    ntype.add_copy_behavior<MyTestNodeStorage>(
        [](MyTestNodeStorage *dst_storage, const MyTestNodeStorage *UNUSED(src_storage)) {
          dst_storage->x += 1;
        });
    ntype.add_draw_fn([](uiLayout *layout, struct bContext *UNUSED(C), struct PointerRNA *ptr) {
      bNode *node = (bNode *)ptr->data;
      MyTestNodeStorage *storage = (MyTestNodeStorage *)node->storage;
      uiBut *but = uiDefButI(uiLayoutGetBlock(layout),
                             UI_BTYPE_NUM,
                             0,
                             "X value",
                             0,
                             0,
                             50,
                             50,
                             &storage->x,
                             -1000,
                             1000,
                             3,
                             20,
                             "my x value");
      uiItemL(layout, "Hello World", 0);
      UI_but_func_set(
          but,
          [](bContext *C, void *UNUSED(arg1), void *UNUSED(arg2)) { update_tree(C); },
          nullptr,
          nullptr);
    });

    ntype.register_type();
  }
  {
    static NodeDefinition ntype("MyTestNode2", "Node 2", "Description");
    ntype.add_declaration([](NodeBuilder &node_builder) {
      node_builder.float_input("a", "A");
      node_builder.float_input("b", "B");
      node_builder.float_output("result", "Result");
    });
    ntype.add_label_fn([](bNodeTree *UNUSED(ntree), bNode *node, char *r_label, int maxlen) {
      if (node->flag & NODE_HIDDEN) {
        BLI_strncpy(r_label, "Custom Label", maxlen);
      }
    });
    ntype.register_type();
  }
  {
    static NodeDefinition ntype("FloatAddNode", "Float Add Node", "");
    ntype.add_dna_storage<FloatAddNodeStorage>(
        "FloatAddNodeStorage",
        []() { return (FloatAddNodeStorage *)MEM_callocN(sizeof(FloatAddNodeStorage), __func__); },
        [](const FloatAddNodeStorage *storage) {
          FloatAddNodeStorage *new_storage = (FloatAddNodeStorage *)MEM_callocN(
              sizeof(FloatAddNodeStorage), __func__);
          LISTBASE_FOREACH (VariadicNodeSocketIdentifier *, value, &storage->inputs_info) {
            void *new_value = MEM_dupallocN(value);
            BLI_addtail(&new_storage->inputs_info, new_value);
          }
          return new_storage;
        },
        [](FloatAddNodeStorage *storage) {
          BLI_freelistN(&storage->inputs_info);
          MEM_freeN(storage);
        });
    ntype.add_declaration([](NodeBuilder &node_builder) {
      FloatAddNodeStorage *storage = node_builder.node_storage<FloatAddNodeStorage>();
      LISTBASE_FOREACH (VariadicNodeSocketIdentifier *, value, &storage->inputs_info) {
        node_builder.float_input(value->identifier, "Value");
      }
      node_builder.operator_input(
          "New Input",
          "New",
          [](bNodeTree *UNUSED(ntree),
             bNode *node,
             bNodeSocket *UNUSED(socket),
             bNodeSocket *UNUSED(directly_linked_socket),
             bNodeSocket *UNUSED(linked_socket)) {
            /* TODO: refresh node and make link */
            FloatAddNodeStorage *storage = get_node_storage<FloatAddNodeStorage>(node);
            VariadicNodeSocketIdentifier *value = (VariadicNodeSocketIdentifier *)MEM_callocN(
                sizeof(VariadicNodeSocketIdentifier), __func__);
            BLI_uniquename(&storage->inputs_info,
                           value,
                           "ID",
                           '.',
                           offsetof(VariadicNodeSocketIdentifier, identifier),
                           sizeof(value->identifier));
            BLI_addtail(&storage->inputs_info, value);
          });
      node_builder.float_output("result", "Result");
    });
    ntype.add_draw_fn([](uiLayout *layout, struct bContext *UNUSED(C), struct PointerRNA *ptr) {
      bNode *node = (bNode *)ptr->data;
      uiBut *but = uiDefBut(uiLayoutGetBlock(layout),
                            UI_BTYPE_BUT,
                            0,
                            "Add Input",
                            0,
                            0,
                            100,
                            40,
                            nullptr,
                            0,
                            0,
                            0,
                            0,
                            "Add new input");
      UI_but_func_set(
          but,
          [](bContext *C, void *arg1, void *UNUSED(arg2)) {
            bNode *node = (bNode *)arg1;
            FloatAddNodeStorage *storage = get_node_storage<FloatAddNodeStorage>(node);
            VariadicNodeSocketIdentifier *value = (VariadicNodeSocketIdentifier *)MEM_callocN(
                sizeof(VariadicNodeSocketIdentifier), __func__);
            BLI_uniquename(&storage->inputs_info,
                           value,
                           "ID",
                           '.',
                           offsetof(VariadicNodeSocketIdentifier, identifier),
                           sizeof(value->identifier));
            BLI_addtail(&storage->inputs_info, value);
            update_tree(C);
          },
          node,
          nullptr);
    });
    ntype.register_type();
  }
}

void init_socket_data_types()
{
  {
    static SocketDefinition stype("NodeSocketFloatList");
    stype.set_color({0.63, 0.63, 0.63, 0.5});
    stype.register_type();
  }
  {
    static SocketDefinition stype("NodeSocketIntList");
    stype.set_color({0.06, 0.52, 0.15, 0.5});
    stype.register_type();
  }
  {
    static SocketDefinition stype("MyIntSocket");
    stype.set_color({0.06, 0.52, 0.15, 1.0});
    stype.register_type();
  }
  {
    static SocketDefinition stype("OperatorSocket");
    stype.set_color({0.0, 0.0, 0.0, 0.0});
    stype.register_type();
  }
  {
    static SocketDefinition stype("MyFloatSocket");
    stype.set_color({1, 1, 1, 1});
    stype.add_dna_storage<bNodeSocketValueFloat>(
        "bNodeSocketValueFloat", [](bNodeSocketValueFloat *storage) { storage->value = 11.5f; });
    stype.add_draw_fn([](bContext *UNUSED(C),
                         uiLayout *layout,
                         PointerRNA *ptr,
                         PointerRNA *UNUSED(node_ptr),
                         const char *UNUSED(text)) {
      bNodeSocket *socket = (bNodeSocket *)ptr->data;
      bNodeSocketValueFloat *storage = get_socket_storage<bNodeSocketValueFloat>(socket);
      uiDefButF(uiLayoutGetBlock(layout),
                UI_BTYPE_NUM,
                0,
                "My Value",
                0,
                0,
                150,
                30,
                &storage->value,
                -1000,
                1000,
                3,
                20,
                "my x value");
    });
    stype.register_type();
  }

  data_socket_float = new BaseSocketDataType("Float", nodeSocketTypeFind("MyFloatSocket"));
  data_socket_int = new BaseSocketDataType("Integer", nodeSocketTypeFind("MyIntSocket"));
  data_socket_float_list = new ListSocketDataType("Float List",
                                                  nodeSocketTypeFind("NodeSocketFloatList"));
  data_socket_int_list = new ListSocketDataType("Integer List",
                                                nodeSocketTypeFind("NodeSocketIntList"));

  data_socket_float->m_list_type = data_socket_float_list;
  data_socket_float_list->m_base_type = data_socket_float;
  data_socket_int->m_list_type = data_socket_int_list;
  data_socket_int_list->m_base_type = data_socket_int;

  socket_data_types = new DataTypesInfo();
  socket_data_types->add_data_type(data_socket_float);
  socket_data_types->add_data_type(data_socket_int);
  socket_data_types->add_data_type(data_socket_float_list);
  socket_data_types->add_data_type(data_socket_int_list);
}

void free_socket_data_types()
{
  delete socket_data_types;
  delete data_socket_float;
  delete data_socket_int;
  delete data_socket_float_list;
  delete data_socket_int_list;
}

using BKE::VInputSocket;
using BKE::VirtualNodeTree;
using BKE::VNode;
using BKE::VOutputSocket;

struct SocketID {
  bNode *bnode;
  eNodeSocketInOut inout;
  std::string identifier;

  friend bool operator==(const SocketID &a, const SocketID &b)
  {
    return a.bnode == b.bnode && a.inout == b.inout && a.identifier == b.identifier;
  }
};

namespace BLI {
template<> class DefaultHash<SocketID> {
 public:
  uint32_t operator()(const SocketID &value) const
  {
    uint32_t h1 = DefaultHash<bNode *>{}(value.bnode);
    uint32_t h2 = DefaultHash<std::string>{}(value.identifier);
    return h1 * 42523 + h2;
  }
};
};  // namespace BLI

static void get_node_declarations(bNodeTree *ntree,
                                  ArrayRef<const VNode *> vnodes,
                                  LinearAllocator<> &allocator,
                                  MutableArrayRef<const NodeDecl *> r_node_decls)
{
  BLI::assert_same_size(vnodes, r_node_decls);

  /* TODO: handle reroute and frames */
  for (uint i : vnodes.index_range()) {
    const VNode &vnode = *vnodes[i];
    bNode *node = vnode.bnode();
    NodeDecl *node_decl = allocator.construct<NodeDecl>(ntree, node);
    node_decl->reserve_decls(allocator, vnode.inputs().size(), vnode.outputs().size());

    NodeBuilder builder{allocator, *node_decl};
    NodeDefinition::declare_node(node, builder);
    r_node_decls[i] = node_decl;
  }
}

static void rebuild_nodes_and_keep_state(ArrayRef<const VNode *> vnodes)
{
  if (vnodes.size() == 0) {
    return;
  }

  const VirtualNodeTree &vtree = vnodes[0]->tree();
  bNodeTree *ntree = vtree.btree();

  Set<std::pair<SocketID, SocketID>> links_to_restore;
  Map<SocketID, std::pair<const SocketDefinition *, void *>> value_per_socket;

  /* Remember socket states. */
  for (const VNode *vnode : vnodes) {
    for (const VInputSocket *vinput : vnode->inputs()) {
      SocketID id_to = {vinput->node().bnode(), SOCK_IN, vinput->identifier()};
      const SocketDefinition *def = SocketDefinition::get_from_socket(vinput->bsocket());
      void *storage_copy = def->get_dna_storage_copy(vinput->bsocket());
      if (storage_copy != nullptr) {
        value_per_socket.add_new(id_to, {def, storage_copy});
      }

      for (const VOutputSocket *voutput : vinput->directly_linked_sockets()) {
        SocketID id_from = {voutput->node().bnode(), SOCK_OUT, voutput->identifier()};
        links_to_restore.add({std::move(id_from), id_to});
      }
    }
    for (const VOutputSocket *voutput : vnode->outputs()) {
      SocketID id_from = {voutput->node().bnode(), SOCK_OUT, voutput->identifier()};
      const SocketDefinition *def = SocketDefinition::get_from_socket(voutput->bsocket());
      void *storage_copy = def->get_dna_storage_copy(voutput->bsocket());
      if (storage_copy != nullptr) {
        value_per_socket.add_new(id_from, {def, storage_copy});
      }

      for (const VInputSocket *vinput : voutput->directly_linked_sockets()) {
        SocketID id_to = {vinput->node().bnode(), SOCK_IN, vinput->identifier()};
        links_to_restore.add({id_from, std::move(id_to)});
      }
    }
  }

  /* Rebuild nodes. */
  LinearAllocator<> allocator;
  Array<const NodeDecl *> node_decls(vnodes.size(), nullptr);
  get_node_declarations(ntree, vnodes, allocator, node_decls);
  for (uint i : vnodes.index_range()) {
    node_decls[i]->build();
  }

  /* Restore links. */
  for (const std::pair<SocketID, SocketID> &link_info : links_to_restore) {
    const SocketID &from_id = link_info.first;
    const SocketID &to_id = link_info.second;
    BLI_assert(from_id.inout == SOCK_OUT);
    BLI_assert(to_id.inout == SOCK_IN);

    bNodeSocket *from_socket = nodeFindSocket(from_id.bnode, SOCK_OUT, from_id.identifier.c_str());
    bNodeSocket *to_socket = nodeFindSocket(to_id.bnode, SOCK_IN, to_id.identifier.c_str());

    if (from_socket && to_socket) {
      nodeAddLink(ntree, from_id.bnode, from_socket, to_id.bnode, to_socket);
    }
  }

  /* Restore socket values. */
  value_per_socket.foreach_item(
      [&](const SocketID &socket_id, const std::pair<const SocketDefinition *, void *> &value) {
        bNodeSocket *socket = nodeFindSocket(
            socket_id.bnode, socket_id.inout, socket_id.identifier.c_str());
        if (socket != nullptr) {
          value.first->free_dna_storage(socket->default_value);
          socket->default_value = value.second;
        }
        else {
          value.first->free_dna_storage(value.second);
        }
      });
}

static bool rebuild_currently_outdated_nodes(VirtualNodeTree &vtree,
                                             ArrayRef<const NodeDecl *> node_decls)
{
  Vector<const VNode *> vnodes_to_update;

  for (uint i : node_decls.index_range()) {
    if (!node_decls[i]->sockets_are_correct()) {
      vnodes_to_update.append(vtree.nodes()[i]);
    }
  }

  rebuild_nodes_and_keep_state(vnodes_to_update);
  return vnodes_to_update.size() > 0;
}

static bool remove_invalid_links(VirtualNodeTree &vtree)
{
  Vector<bNodeLink *> links_to_remove;
  for (const VInputSocket *vinput : vtree.all_input_sockets()) {
    for (bNodeLink *link : vinput->incident_links()) {
      if (link->fromsock->typeinfo != vinput->bsocket()->typeinfo) {
        links_to_remove.append(link);
      }
    }
  }

  for (bNodeLink *link : links_to_remove) {
    nodeRemLink(vtree.btree(), link);
  }

  return links_to_remove.size() > 0;
}

static bool run_one_operator_socket(const VirtualNodeTree &vtree,
                                    ArrayRef<const NodeDecl *> node_decls)
{
  bNodeTree *ntree = vtree.btree();

  for (uint node_index : node_decls.index_range()) {
    const NodeDecl *node_decl = node_decls[node_index];
    if (node_decl->m_has_operator_input) {
      const VNode *vnode = vtree.nodes()[node_index];
      for (uint input_index : vnode->inputs().index_range()) {
        SocketDecl *socket_decl = node_decl->m_inputs[input_index];
        if (socket_decl->category() == SocketDeclCategory::Operator) {
          const VInputSocket &vinput = vnode->input(input_index);

          if (vinput.directly_linked_sockets().size() == 1 &&
              vinput.linked_sockets().size() == 1) {
            bNodeLink *link = vinput.incident_links()[0];
            nodeRemLink(ntree, link);

            bNodeSocket *directly_linked_socket = vinput.directly_linked_sockets()[0]->bsocket();
            bNodeSocket *linked_socket = vinput.linked_sockets()[0]->bsocket();

            OperatorSocketDecl *operator_decl = (OperatorSocketDecl *)socket_decl;
            OperatorSocketFn callback = operator_decl->callback();
            if (callback) {
              callback(
                  ntree, vnode->bnode(), vinput.bsocket(), directly_linked_socket, linked_socket);
            }
            return true;
          }
          else if (vinput.incident_links().size() > 1) {
            for (bNodeLink *link : vinput.incident_links()) {
              nodeRemLink(ntree, link);
            }
            return true;
          }
        }
      }
    }
  }
  return false;
}

static bool run_operator_sockets(VirtualNodeTree &vtree, ArrayRef<const NodeDecl *> node_decls)
{
  bNodeTree *ntree = vtree.btree();
  bool tree_changed = false;

  while (true) {
    bool found_an_operator_socket = run_one_operator_socket(vtree, node_decls);
    if (found_an_operator_socket) {
      tree_changed = true;
      vtree.~VirtualNodeTree();
      new (&vtree) VirtualNodeTree(ntree);
    }
    else {
      break;
    }
  }

  return tree_changed;
}

void update_sim_node_tree(bNodeTree *ntree)
{
  VirtualNodeTree vtree(ntree);
  LinearAllocator<> allocator;

  Array<const NodeDecl *> node_decls(vtree.nodes().size());
  get_node_declarations(ntree, vtree.nodes(), allocator, node_decls);

  if (rebuild_currently_outdated_nodes(vtree, node_decls)) {
    vtree.~VirtualNodeTree();
    new (&vtree) VirtualNodeTree(ntree);
  }
  if (run_operator_sockets(vtree, node_decls)) {
    vtree.~VirtualNodeTree();
    new (&vtree) VirtualNodeTree(ntree);
  }
  remove_invalid_links(vtree);
}
