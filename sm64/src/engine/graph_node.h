/* new64 -- scene graph node types live in types.h; this is the include path. */
#ifndef NEW64_GRAPH_NODE_H
#define NEW64_GRAPH_NODE_H

#include "types.h"

#define GRAPH_RENDER_ACTIVE         (1 << 0)
#define GRAPH_RENDER_CHILDREN_FIRST (1 << 1)
#define GRAPH_RENDER_BILLBOARD      (1 << 2)
#define GRAPH_RENDER_INVISIBLE      (1 << 4)
#define GRAPH_RENDER_HAS_ANIMATION  (1 << 5)

#endif /* NEW64_GRAPH_NODE_H */
