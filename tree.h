#define _TREE_VLC 0
#define _TREE_RUN 1
#define _TREE_VAL 2

struct Tree{
    int val;
    struct Tree *node[2];
};typedef struct Tree Tree;

Tree* tree_build( int, int*);

