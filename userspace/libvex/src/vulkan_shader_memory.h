/* Private interpreter continuation and checked shader memory. Shared by the
   graphics stages and compute; no shader ABI or kernel shader execution. */













/* The CPU reference backend implements stronger ordering (seq_cst) for all
   supported GLSL450 memory orders. Unknown scopes/storage semantics are never
   erased. Workgroup control barriers are executed by the lane rendezvous. */






/* Validate scopes, semantics and feature-gated writes before publishing a
   pipeline, rather than accepting a shader that can only fail mid-submit. */

