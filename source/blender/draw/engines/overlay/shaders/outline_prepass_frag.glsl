
flat in int objectId;

/* using uint because 16bit uint can contain more ids than int. */
out uint outId;

void main()
{
#ifdef USE_GPENCIL
  if (stroke_round_cap_mask(strokePt1, strokePt2, strokeThickness) < 0.001) {
    discard;
  }
#endif

  outId = uint(objectId);
}
