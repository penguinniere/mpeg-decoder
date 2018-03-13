#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <climits>
#include "tree.h"

/***
tree_build( int, int*)
輸入陣列大小和陣列(size*3)，定義在tree_data.h
產生huffman樹，並回傳tree root。
如果節點沒值 value用INT_MIN表示
***/
Tree* tree_build( int size, int *treeData){
    Tree *head = NULL, *current = NULL;
    head = (Tree*) malloc( sizeof(Tree));
    memset( head, 0, sizeof(Tree));
    head->val = INT_MIN;
    //根據size建立n個有值的點
    for( int i = 0; i < size; i++){
        current = head;
        int vlc = treeData[3*i+_TREE_VLC], m;
        //根據huffman編碼造樹，在造樹的最後節點放值
        for( int k = (0x1 << (treeData[3*i+_TREE_RUN]-1)); k>0; k>>=1){
            m = (vlc&k)? 1: 0;
            //如果沒有點，建立新的節點
            if( !current->node[m]){
                current->node[m] = (Tree*) malloc( sizeof(Tree));
                memset( current->node[m], 0, sizeof(Tree));
                current->node[m]->val = INT_MIN;
            }
            current = current->node[m];        
        }
        current->val = treeData[3*i+_TREE_VAL];
    }
    return head;
}