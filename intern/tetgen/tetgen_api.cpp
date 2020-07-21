
#include "tetgen_api.h"
#include "tetgen.h"
#include "MEM_guardedalloc.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <vector>

void init_tetgenremeshdata(TetGenRemeshData *data)
{
    data->in_verts = NULL;
    data->in_faces = NULL;
    data->in_totverts = 0;
    data->in_totfaces = 0;
    data->out_verts = NULL;
    data->out_facets = NULL;
    data->out_tets = NULL;
    data->out_totverts = 0;
    data->out_totfacets = 0;
    data->out_tottets = 0;
}

// Finds the largest edge length of the mesh and computes the volume
// if that were an edge of a tet using (e^3 / (6*sqrt(2)))
static float compute_maxvol(float *verts, unsigned int *faces, int num_faces)
{
    auto squared_norm = [](float *v0, float *v1)
    {
        return (v0[0]-v1[0])*(v0[0]-v1[0]) +
            (v0[1]-v1[1])*(v0[1]-v1[1]) +
            (v0[2]-v1[2])*(v0[2]-v1[2]);
    };
    float max_sq_edge_len = 0;
    for (int i=0; i<num_faces; ++i)
    {
        unsigned int f[3] = {faces[i*3], faces[i*3+1], faces[i*3+2]};
        float v0[3] = {verts[f[0]*3], verts[f[0]*3+1], verts[f[0]*3+2]};
        float v1[3] = {verts[f[1]*3], verts[f[1]*3+1], verts[f[1]*3+2]};
        float v2[3] = {verts[f[2]*3], verts[f[2]*3+1], verts[f[2]*3+2]};
        float max_sq_e = std::max(std::max(
            squared_norm(v0,v1),
            squared_norm(v1,v2)),
            squared_norm(v2,v0));

        if( max_sq_e > max_sq_edge_len)
            max_sq_edge_len = max_sq_e;
    }

    double e = std::sqrt(max_sq_edge_len);
    return (e*e*e) / (6.f*std::sqrt(2.f));

} // end compute maxvol

static void make_tetgenio(
        float *verts,
        unsigned int *faces,
        int numverts,
        int numfaces,
		tetgenio &tgio )
{
    tgio.initialize();
	tgio.firstnumber = 0;
	tgio.mesh_dim = 3;
	tgio.numberofpoints = numverts;
	tgio.pointlist = new REAL[tgio.numberofpoints * 3];
//	tgio.pointlist = (REAL *)MEM_malloc_arrayN(
//		tgio.numberofpoints, 3 * sizeof(REAL), "tetgen remesh out verts");
	for (int i=0; i < tgio.numberofpoints; ++i)
    {
		tgio.pointlist[i*3+0] = verts[i*3+0];
		tgio.pointlist[i*3+1] = verts[i*3+1];
		tgio.pointlist[i*3+2] = verts[i*3+2];
	}
	tgio.numberoffacets = numfaces;
	tgio.facetlist = new tetgenio::facet[tgio.numberoffacets];
//	tgio.facetlist = (tetgenio::facet *)MEM_malloc_arrayN(
//		tgio.numberoffacets, sizeof(tetgenio::facet), "tetgen remesh out facets");  
	tgio.facetmarkerlist = new int[tgio.numberoffacets];
//	tgio.facetmarkerlist = (int *)MEM_malloc_arrayN(
//		tgio.numberoffacets, sizeof(int), "tetgen remesh out marker list");
	for (int i=0; i<numfaces; ++i)
    {
		tgio.facetmarkerlist[i] = i;
		tetgenio::facet *f = &tgio.facetlist[i];
		f->numberofholes = 0;
		f->holelist = NULL;
		f->numberofpolygons = 1;
		f->polygonlist = new tetgenio::polygon[1];
		tetgenio::polygon *p = &f->polygonlist[0];
		p->numberofvertices = 3;
		p->vertexlist = new int[3];
		p->vertexlist[0] = faces[i*3+0];
		p->vertexlist[1] = faces[i*3+1];
		p->vertexlist[2] = faces[i*3+2];
	}
}

bool tetgen_resmesh(TetGenRemeshData *tg)
{
//	float maxvol = compute_maxvol(tg->in_verts, tg->in_faces, tg->in_totfaces);
    float quality = 1.4;

	// Set up the switches
	std::stringstream switches;
//	switches << "Q"; // quiet
//    switches << "a" << maxvol;
//	if (quality>0)
//        switches << "q" << quality;


    tetgenio in;
    make_tetgenio(tg->in_verts, tg->in_faces, tg->in_totverts, tg->in_totfaces, in);
	tetgenio out;
    out.initialize();
    char *c_switches = (char *)switches.str().c_str();
	tetrahedralize(c_switches, &in, &out);

	if( out.numberoftetrahedra == 0 || out.numberofpoints == 0 )
    {
        printf("\n\n\n\n*****FAILED TETGEN\n");
		return false;
	}

    // We'll create our custom list of facets to render
    // with blender. These are all of the triangles that
    // make up the inner and outer faces, without duplicates.
    // To avoid duplicates, we'll hash them as a string.
    // While not super efficient, neither is tetrahedralization...
    // TODO
    struct face {
        int f0, f1, f2;
        face(int f0_, int f1_, int f2_) : f0(f0_), f1(f1_), f2(f2_) {}
    };
    auto face_hash = [](int f0, int f1, int f2){
        return std::to_string(f0)+" "+std::to_string(f1)+" "+std::to_string(f2);
    };
    std::unordered_map<std::string,face> faces;

    // Tets:
    tg->out_tottets = out.numberoftetrahedra;
	tg->out_tets = (unsigned int *)MEM_malloc_arrayN(
		out.numberoftetrahedra, 4 * sizeof(unsigned int), "tetgen remesh tets");
	for (int i=0; i<out.numberoftetrahedra; ++i)
    {
        tg->out_tets[i*4+0] = out.tetrahedronlist[i*4+0];
        tg->out_tets[i*4+1] = out.tetrahedronlist[i*4+1];
        tg->out_tets[i*4+2] = out.tetrahedronlist[i*4+2];
        tg->out_tets[i*4+3] = out.tetrahedronlist[i*4+3];

        // Append faces
        for(int j=0; j<4; ++j)
        {
            int f0 = tg->out_tets[i*4+j];
            int f1 = tg->out_tets[i*4+(j+1)%4];
            int f2 = tg->out_tets[i*4+(j+2)%4];
            std::string hash = face_hash(f0,f1,f2);
            if (faces.count(hash)!=0)
                continue;
            
            faces.emplace(hash, face(f0,f1,f2));
        }
	}

    // Faces:
    tg->out_totfacets = faces.size();
	tg->out_facets = (unsigned int *)MEM_malloc_arrayN(
		tg->out_totfacets, 3 * sizeof(unsigned int), "tetgen remesh facets");
    int f_idx = 0;
    for (std::unordered_map<std::string,face>::iterator it = faces.begin();
        it != faces.end(); ++it, ++f_idx)
    {
        tg->out_facets[f_idx*3+0] = it->second.f0;
        tg->out_facets[f_idx*3+1] = it->second.f1;
        tg->out_facets[f_idx*3+2] = it->second.f2;    
    }

    // Vertices:
    tg->out_totverts = out.numberofpoints;
	tg->out_verts = (float *)MEM_malloc_arrayN(
		out.numberofpoints, 3 * sizeof(float), "tetgen remesh verts");
	for (int i=0; i<out.numberofpoints; ++i)
    {
        tg->out_verts[i*3+0] = out.pointlist[i*3+0];
        tg->out_verts[i*3+1] = out.pointlist[i*3+1];
        tg->out_verts[i*3+2] = out.pointlist[i*3+2];
	}

    return true;
}
