#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <algorithm>
#include <vector>
#include <thread>
#include <ctime>
#include <GL/glut.h>
using namespace std;

#include "header.h"
#include "tree.h"
#include "tree_data.h"


#define _MAX_DATA_SIZE 4
#define _BLOCK_SIZE 8
#define _BLOCK_SIZE2 64
#define _BLOCK_IN_MAC 6
#define _R 0
#define _G 1
#define _B 2
//define start code
#define _PICTURE_START_CODE	   0x00000100
#define _SLICE_START_CODE_MIN  0x00000101 
#define _SLICE_START_CODE_MAX  0x000001AF
//reserved	000001B0
//reserved	000001B1
#define _USER_DATA_START_CODE  0x000001B2
#define _SEQUENCE_START_CODE   0x000001B3
#define _SEQUENCE_ERROR_CODE   0x000001B4
#define _EXTENSION_START_CODE  0x000001B5
//reserved	000001B6
#define _SEQUENCE_END_CODE     0x000001B7
#define _GOP_START_CODE        0x000001B8
#define _SYSTEM_START_CODE_MIN 0x000001B9
#define _SYSTEM_START_CODE_MAX 0x000001FF

//idct macro
#define xadd3(xa,xb,xc,xd,h) p=xa+xb, n=xa-xb, xa=p+xc+h, xb=n+xd+h, xc=p-xc+h, xd=n-xd+h // triple-butterfly-add (and possible rounding)
#define xmul(xa,xb,k1,k2,sh) n=k1*(xa+xb), p=xa, xa=(n+(k2-k1)*xb)>>sh, xb=(n-(k2+k1)*p)>>sh
//header and variable
//vector<long> sequenceOffset;
FILE* mpegFile = NULL;
int block[_BLOCK_IN_MAC][_BLOCK_SIZE2], blockRecon[3][_BLOCK_IN_MAC][_BLOCK_SIZE2];
char data[_MAX_DATA_SIZE];
char stream;
unsigned char streamOffset = 0x0;
int prevDCValue[3], macroBlockAddress, prevIntraAddress, prevMacroType[2];
int prevMotion[2][2], motion[2][2];
int *pictureY[3] = {0}, *pictureCb[3] = {0}, *pictureCr[3]={0}, *pictureRGB = 0;
int pictureNo[3] ={-1, -1, -1}, displayCounter=0;
unsigned char *picture = 0; 
int quantizerScale;
int multiplexCode = 0;
int counter = 0;
Seq seq;
Gop gop;
Pic pic;
Sli sli;
Mac mac;

//gui part
vector <unsigned char*> decodedFrame;

//huffman tree 
Tree *macroBlockIncremental = NULL;
Tree *macroBlockType[4] = {0};
Tree *macroBlockMotion = NULL;
Tree *macroBlockPattern = NULL;
Tree *dctDCLuminanceSize = NULL;
Tree *dctDCChrominanceSize = NULL;
Tree *dctCoefficientFirst = NULL;
Tree *dctCoefficientNext = NULL;

//process each layer
void decode();
void sequence_layer();
void gop_layer();
void picture_layer();
void slice_layer();
void macroblock_layer();
void block_layer(int);
void dummy();

//common function
inline int sign(int);
inline void read_stream();
inline void read_bits( int);
inline void read_bytes( int);
inline void peek_bits( int);
inline void peek_bytes( int);
inline void char_to_int( int, int*);
inline int run_huffman_tree( Tree*);
inline void build_huffman_tree();

//block decoded
inline void skip_block();
inline void combine_blockRecon();
inline void block_to_picture();
inline void ycbcr_to_bgr(int);
inline void print_picture(int);
inline void save_picture(int);
inline void swap_picture(int, int);
inline void reset_motion(int);

inline void decode_intra_block();
inline void decode_pattern_block();
inline void calculate_motion(int);
inline void decode_motion_block(int);

//idct function in halicery.com/Image/idct.html
inline void idct_1d(int*, int*, int, int);
inline void idct_2d(int*);

//gui part
int gui( int , char **);

/***
在這裡開一個thread用來解碼，main thread則執行GUI相關函式
***/
int main( int argc, char *argv[]){
    //mpeg只處理一個參數
    if( argc!=2){
        fprintf( stderr, "No specify file\n");
        exit(1);
    }

    mpegFile = fopen(argv[1], "rb");
    //確定檔案室否成功開啟
    if( mpegFile ==NULL){
        fprintf( stderr, "Can't open file\n");
        exit(1);
    }
    
    fseek( mpegFile, 0, SEEK_SET);
    build_huffman_tree();
    
    thread decodeThread( decode);
    gui( argc, argv);
    decodeThread.join();
    return 0;
}


/****
layer_part()
****/

/***
編碼的最上層，看是要進入 seq, gop, pic,還是 user data 或 extension 直到seq end結束迴圈
***/
void decode(){
    //using for multiplexing in different layer
    while(1){
        read_bytes( 4);
        char_to_int( 4, &multiplexCode);
        if( multiplexCode == _SEQUENCE_START_CODE)
            sequence_layer();
        else if( multiplexCode == _GOP_START_CODE)
            gop_layer();
        else if( multiplexCode == _PICTURE_START_CODE)
            picture_layer();
        else if( multiplexCode == _USER_DATA_START_CODE)
            dummy();
        else if( multiplexCode == _EXTENSION_START_CODE)
            dummy();
        else if( multiplexCode == _SEQUENCE_END_CODE){
            //print_picture(_BACKWARD);
            save_picture(_BACKWARD);
            break;
        }
        else
            break;
    }
}
/***
處理sequnce layer header
***/
void sequence_layer(){
    int value;
    memset( &seq, 0, sizeof(Seq));
    
    //horizontal size
    read_bits( 12);
    char_to_int( 2, &seq.size[_HORIZONTAL]);
    seq.macroSize[_HORIZONTAL] = (seq.size[0]+15)>>4;
    
    //vertical size
    read_bits( 12);
    char_to_int( 2, &seq.size[_VERTICAL]);
    seq.macroSize[_VERTICAL] = (seq.size[_VERTICAL])+15>>4;
    
    //pel aspect ratio
    read_bits( 4);
    char_to_int( 1, &seq.pelAspectRatio);
    
    //picture rate
    read_bits(4);
    char_to_int( 1, &seq.pictureRate); 
    
    //bit rate
    read_bits(18);
    char_to_int( 3, &seq.bitRate);
    
    //marker_bit
    read_bits(1);
    char_to_int( 1, &value);
    
    //vbv_buffer_size
    read_bits(10);
    char_to_int( 2, &seq.vbvBufferSize);

    //constrained parameter flat
    read_bits(1);//CPF
    char_to_int( 1, &seq.constrain);

    //load intra quantizer matrix or default
    read_bits(1);
    char_to_int( 1, &value);
    if( !value)
        memcpy( seq.intraQuantizer, defaultIntraQuantizer, sizeof(int)*64);
    else{
        for( int i=0; i<64; i++){
            read_bits(8);
            char_to_int( 1, &value);
            seq.intraQuantizer[i] = value;
        }
    }
    
    //load non intra quantizer or default
    read_bits(1);
    char_to_int( 1, &value);
    if( !value)
        memcpy( seq.nonIntraQuantizer, defaultNonIntraQuantizer, sizeof(int)*64);
    else{
        for( int i=0; i<64; i++){
            read_bits(8);
            char_to_int( 1, &value);
            seq.nonIntraQuantizer[i] =  value;
        }
    }
    
    //may need to write into function
    //initialize picture
    //產生 forward current backward 3-frame buffer, 各有Y Cb Cr
    for( int i=0; i<3; i++){ 
        pictureY[i] = (int*) malloc( 256*seq.macroSize[0]*seq.macroSize[1]*sizeof(int));
        pictureCb[i] = (int*) malloc( 64*seq.macroSize[0]*seq.macroSize[1]*sizeof(int));
        pictureCr[i] = (int*) malloc( 64*seq.macroSize[0]*seq.macroSize[1]*sizeof(int));
    }
    pictureRGB = (int*) malloc( 256*seq.macroSize[0]*seq.macroSize[1]*sizeof(int)*3);
    //picture = (unsigned char*) malloc( 256*seq.macroSize[0]*seq.macroSize[1]*sizeof(unsigned char)*3);

}
/***
處理group of picture layer header
***/
void gop_layer(){
    int test;
    memset( &gop, 0, sizeof(Gop));
    
    //time codec
    read_bits(25);
    char_to_int( 4, &test);
    
    //close group
    read_bits(1);
    char_to_int( 1, &test);
    
    //broken group
    read_bits(1);
    char_to_int( 1, &test);
    
}
/***
處理picture layer header , 並往下呼叫 slice layer
***/
void picture_layer(){
    int value;
    counter++;
    
    //printf("###%d\n", counter);
    //initialize 
    prevIntraAddress = -2;
    memset( &pic, 0, sizeof(Pic));
    
    //temporal reference
    read_bits(10);
    char_to_int(2, &pic.temperal);
 
    //picture coding type
    read_bits(3);
    char_to_int(1, &pic.codingType);

    //vbv delay
    read_bits(16);//VBD
    char_to_int(2, &pic.vbvDelay);
    
    //P B frame 讀取 forward vector
    if( pic.codingType == _PICTURE_CODING_TYPE_P || pic.codingType == _PICTURE_CODING_TYPE_B){
        //full pell forward?
        read_bits(1);
        char_to_int(1, &pic.fullPel[_FORWARD]);
        
        //forward code
        read_bits(3);
        char_to_int(1, &pic.code[_FORWARD]);
        pic.size[_FORWARD] = pic.code[_FORWARD]-1;
        pic.code[_FORWARD] = ( 1 << pic.size[_FORWARD]);
    }
    
    //B frame 讀取 backward vector
    if( pic.codingType == _PICTURE_CODING_TYPE_B){
        //full pell backward?
        read_bits(1);
        char_to_int(1, &pic.fullPel[_BACKWARD]);
        
        //backward code
        read_bits(3);
        char_to_int(1, &pic.code[_BACKWARD]);
        pic.size[_BACKWARD] = pic.code[_BACKWARD]-1;
        pic.code[_BACKWARD] = ( 1 << pic.size[_BACKWARD]);
    }
    
    //extra information picture
    while(1){
        read_bits(1);
        char_to_int( 1, &value);
        if( !value)
            break;
        else{
            read_bits(8);
            char_to_int( 1, &value);
        }
    }
    
    //three frame buffer swap before decode
    if(  pic.codingType == _PICTURE_CODING_TYPE_I || pic.codingType == _PICTURE_CODING_TYPE_P ){
        swap_picture(_FORWARD, _BACKWARD); //_FORWARD = _BACKWARD
        //print_picture(_FORWARD);
        save_picture(_FORWARD);
    }
    pictureNo[_CURRENT] = counter;
    
    //decode block - move to slice, 進入到slice層
    while(1){
        peek_bytes(4);
        char_to_int(4, &multiplexCode);
        //判斷start code是否屬於slice start code
        if (multiplexCode >= _SLICE_START_CODE_MIN && multiplexCode <= _SLICE_START_CODE_MAX){
            read_bytes(4);
            slice_layer();
        }
        else
            break;
    }
    
    //three frame buffer change after decoded
    if(  pic.codingType == _PICTURE_CODING_TYPE_I || pic.codingType == _PICTURE_CODING_TYPE_P ){
        swap_picture( _FORWARD, _BACKWARD); //_FORWARD = _BACKEARD
        swap_picture( _BACKWARD, _CURRENT); //_CURRENT =_BACKWARD
        swap_picture( _FORWARD, _CURRENT);
    }
    else if( pic.codingType == _PICTURE_CODING_TYPE_B){
        //print_picture(_CURRENT);
        save_picture(_CURRENT);
    }
}
/***
處理slice layer header, 並往下呼叫 macroblock layer
***/
void slice_layer(){
    int value;
    memset( &sli, 0, sizeof(Sli));
    
    //vertical
    sli.vertical = multiplexCode & 0x000000FF;
    
    //quantizerScale
    read_bits(5);//QS
    char_to_int( 1, &quantizerScale);
    
    //extra information data
    while(1){
        read_bits(1);
        char_to_int( 1, &value);
        if( !value)
            break;
        else{
            read_bits(8);
            char_to_int( 1, &value);
        }
    }
    
    //initialize
    macroBlockAddress = (sli.vertical-1)*seq.macroSize[0]-1;
    prevDCValue[0] = 1024;
    prevDCValue[1] = 1024;
    prevDCValue[2] = 1024;
    reset_motion(_FORWARD);
    reset_motion(_BACKWARD);
    prevMacroType[_FORWARD] = ( pic.codingType == _PICTURE_CODING_TYPE_P)?  1: 0;
    prevMacroType[_BACKWARD] = 0;
    //move to macroblock layer
    while(1){
        macroblock_layer();
        peek_bits(23);
        char_to_int( 3, &value);
        if(!value) //all bit is zeros
            break;
    }
}
/***
處理macroblock, 並往下呼叫 block layer
***/
void macroblock_layer(){
    int value;

    //initialization
    memset( blockRecon, 0, 3 * _BLOCK_IN_MAC * _BLOCK_SIZE2 * sizeof(int));
    int increment = 0;
    //read macroblock stuffing
    while(1){
        peek_bits(11);
        char_to_int( 2, &value);
        if( value!=0x0F) //000 0000 1111
            break;
        read_bits(11);
    }
   
    //read macroblock escape and process macroBlockAddress incremental
    while(1){
        peek_bits(11);
        char_to_int( 2, &value);
        if( value!=0x08) //000 0000 1000
            break;
        increment += 33;
        read_bits(11);
    }
    increment += run_huffman_tree( macroBlockIncremental);
    //處理skip macroblock
    for( int i = increment; i>1; i--){
        macroBlockAddress++;
        skip_block();
        memset( blockRecon, 0, 3 * _BLOCK_IN_MAC * _BLOCK_SIZE2 * sizeof(int));
    }
    macroBlockAddress++;
    
    memset( &mac, 0, sizeof(Mac));
    //macroBlock Type
    mac.type = run_huffman_tree( macroBlockType[pic.codingType-1]);
    
    //讀取quantizerScale
    if( mac.type & _TYPE_QUANT){
        read_bits(5);
        char_to_int( 1, &quantizerScale);
    }
    
    //reset move vector and reference picture according to picture coding type
    if( pic.codingType == _PICTURE_CODING_TYPE_P && !( mac.type & _TYPE_FORWARD)){ 
        prevMacroType[_FORWARD] = 1;
        reset_motion(_FORWARD);
    }
    else if( pic.codingType == _PICTURE_CODING_TYPE_B){
        prevMacroType[_FORWARD] = 0;
        prevMacroType[_BACKWARD] = 0;   
        if ( mac.type & _TYPE_INTRA){
            reset_motion(_FORWARD);
            reset_motion(_BACKWARD);
        }
    }
    
    //讀取forward vector並解碼
    if( mac.type & _TYPE_FORWARD){
        //讀取 forward horizontal 
        mac.code[_FORWARD][_HORIZONTAL] = run_huffman_tree( macroBlockMotion);
        if( pic.code[_FORWARD] !=1 && mac.code[_FORWARD][_HORIZONTAL]){
            read_bits( pic.size[_FORWARD]);
            char_to_int( 1, &mac.motion[_FORWARD][_HORIZONTAL]);
        }
        //讀取 forward vertical
        mac.code[_FORWARD][_VERTICAL] = run_huffman_tree( macroBlockMotion);
        if( pic.code[_FORWARD] !=1 && mac.code[_FORWARD][_VERTICAL]){
            read_bits( pic.size[_FORWARD]);
            char_to_int( 1, &mac.motion[_FORWARD][_VERTICAL]);
        }
        calculate_motion(_FORWARD); 
        prevMacroType[_FORWARD] = 1;
    } 
    //讀取backward vector並解碼
    if( mac.type & _TYPE_BACKWARD){
        //讀取horizontal
        mac.code[_BACKWARD][_HORIZONTAL] = run_huffman_tree( macroBlockMotion);
        if( pic.code[_BACKWARD] !=1 && mac.code[_BACKWARD][_HORIZONTAL]){
            read_bits( pic.size[_BACKWARD]);
            char_to_int( 1, &mac.motion[_BACKWARD][_HORIZONTAL]);
        }
        //讀取vertical
        mac.code[_BACKWARD][_VERTICAL] = run_huffman_tree( macroBlockMotion);
        if( pic.code[_BACKWARD] !=1 &&  mac.code[_BACKWARD][_VERTICAL]){
            read_bits( pic.size[_BACKWARD]);
            char_to_int( 1, &mac.motion[_BACKWARD][_VERTICAL]);
        }
        calculate_motion(_BACKWARD); 
        prevMacroType[_BACKWARD] = 1;
    }
    //根據pattern or intra 決定block編碼是否在macroblock裡
    if( mac.type & _TYPE_PATTERN)
        mac.pattern = run_huffman_tree( macroBlockPattern);
    else if( mac.type & _TYPE_INTRA)
        mac.pattern = 0x3F;
    
    //move into block
    for( int i=0; i<6; i++)
        block_layer(i);
        
    //如果coding type D，則讀取end of macroblok
    if( pic.codingType == _PICTURE_CODING_TYPE_D){
        //end of macroblock
        read_bits(1);
        char_to_int( 1, &value);
    }
    else{
        //如果type為intra做intra解碼
        if( mac.type & _TYPE_INTRA)
            decode_intra_block();
        else{
            //如果type有pattern,做 pattern編碼
            if( mac.type & _TYPE_PATTERN)
                decode_pattern_block();
            //如果參照forward picture，則搬移forward資料
            if( prevMacroType[_FORWARD])
                decode_motion_block(_FORWARD);
            //如果參照backward picture，則搬移backward資料
            if( prevMacroType[_BACKWARD])
                decode_motion_block(_BACKWARD);
            combine_blockRecon();
        }
        block_to_picture();
    }
}
/***
處理 block， 進行DCT讀取
***/
void block_layer(int no){
    int blockOffset = 0, diffSize, signValue, value;
    //initialize
    memset( block[no], 0, sizeof(float)*_BLOCK_SIZE2);
    //如果有block[i]資料則進行讀取
    if( mac.pattern & (0x20 >> no)){
        //如果type intra進行 DC編碼
        if( mac.type & _TYPE_INTRA){
            if( no<4) //Y
                diffSize = run_huffman_tree( dctDCLuminanceSize);
            else //Cb Cr
                diffSize = run_huffman_tree( dctDCChrominanceSize);
            //計算diff size值    
            if( diffSize){
                read_bits(diffSize);
                char_to_int( 1, &value);
                block[no][0] = ( value & ( 1 << (diffSize-1)))?  value: ((-1<<diffSize) | (value+1));
            }
        }
        //反之則進行 coefficient first AC編碼
        else{
            value = run_huffman_tree( dctCoefficientFirst);
            //EOB -不做任何事
            if( value == _END_OF_BLOCK)
                ;
            //非escape情況下直接解出 run 和 level
            else if( value != _ESCAPE){
                read_bits(1);
                char_to_int( 1, &signValue);
                blockOffset = (value&_RUN);
                block[no][blockOffset] = (signValue)? -(value>>_LEVEL): (value>>_LEVEL);
            }
            //如果 escpae 做處理解出 run 和 level
            else{
                read_bits(6);
                char_to_int( 1, &value);
                blockOffset = value;
                read_bits(8);
                value = (int) data[0];
                //當level為-128或是0 則多讀取8bit進行判斷
                if( value == -128 || value == 0){
                    read_bits(8);
                    value &= 0xFFFFFF00;
                    value |= (data[0] & 0xFF);
                }
                block[no][blockOffset] = value;
            }
        }
        //如果不是D type，進行AC next編碼
        if( pic.codingType != _PICTURE_CODING_TYPE_D){
            while (1){
                value = run_huffman_tree( dctCoefficientNext);
                //End of block結束迴圈
                if( value ==_END_OF_BLOCK)
                    break;
                //非escape情況下直接解出 run 和 level
                if( value != _ESCAPE){
                    read_bits(1);
                    char_to_int( 1, &signValue);
                    blockOffset += (value&_RUN)+1;
                    block[no][blockOffset] = (signValue)? -(value>>_LEVEL): (value>>_LEVEL);
                }
                //如果 escpae 做處理解出 run 和 level
                else{
                    read_bits(6);
                    char_to_int( 1, &value);   
                    blockOffset += value+1;               
                    read_bits(8);
                    value = (int) data[0];
                    //當level為-128或是0 則多讀取8bit進行判斷
                    if( value == -128 || value == 0){
                        read_bits(8); 
                        value &= 0xFFFFFF00;
                        value |= (data[0] & 0xFF);
                    }
                    block[no][blockOffset] = value;    
                }
            }
        }
    }
}
/***
用來處理user start code和 extesion start code，沒特別用途。可能需要修正加快讀取速度
***/
void dummy(){
    int test;
    while(1){
        peek_bits(24);
        char_to_int( 3, &test);
        //判斷下三個byte是否屬於start code前3bytes
        if( test==1)
            break;
        else
            read_bytes(1);
    }
}


/***
    common part
***/

/***
如果正數回傳1, 負數回傳-1, 0則回傳0
***/
inline int sign( int a){
    return (a>0)? 1: (a<0)? -1: 0;
}
/***
從mpeg檔案讀1 byte資料，並重置streamOffset讓下一次bit讀取leftmost bit
***/
inline void read_stream(){
    fread( &stream, sizeof(char), 1, mpegFile);
    streamOffset = (0x80); 
}
/***
讀取 size 個 bits 的資料放在全域變數data
***/
inline void read_bits( int size){
    int rightmost = ((size-1) >> 3);
    int index;
    memset( data, 0, sizeof(char)*_MAX_DATA_SIZE);
    //讀一個個bit，直到i=0
    for( int i = size-1; i>=0; i--, streamOffset>>=1){
        //表示需要從檔案讀取下一個byte
        if(!streamOffset) read_stream();
        index = rightmost - (i>>3);
        data[index] <<= 1;
        //判斷讀取的bit是否為1
        if( stream & streamOffset)
            data[index] ++;
    }
}
/***
讀取 size 個 bytes 並忽略前一個stream剩下來的bit
***/
inline void read_bytes( int size){
    fread( data, sizeof(char), size, mpegFile);
    streamOffset = 0;
}
/***
讀取 size 個 bits 之後回到呼叫前的檔案狀態
***/
inline void peek_bits( int size){
    long currentFileOffset = ftell( mpegFile);
    unsigned char currentStreamOffset = streamOffset;
    char currentStream = stream;
    
    read_bits( size);
    fseek( mpegFile, currentFileOffset, SEEK_SET);
    streamOffset = currentStreamOffset;
    stream = currentStream;
}
/***
讀取 size 個 bytes 之後回到呼叫前的檔案狀態
***/
inline void peek_bytes( int size){
    long currentFileOffset = ftell( mpegFile);
    unsigned char currentStreamOffset = streamOffset;
    char currentStream = stream;
    
    read_bytes( size);
    fseek( mpegFile, currentFileOffset, SEEK_SET);
    streamOffset = currentStreamOffset;
    stream = currentStream;
}
/***
將data裡的東西轉為int， size表示考慮從data[0]~data[size-1]的資料轉換
***/
inline void char_to_int( int size, int* intD){
    *intD = 0;
    //mpeg最高位放左邊所以根據size左移8位和加上下一個data資料
    for( int i=0; i < size; i++){
        *intD <<= 8;
        *intD |= (data[i] & 0xFF);
    }    
}
/***
給huffman tree root，回傳在humman tree的數值
***/
inline int run_huffman_tree( Tree *head){
    Tree *current = head;
    int m;
    //INT_MIN表示該節點沒有數值
    while( current->val==INT_MIN){
        //需要從檔案read bytes
        if(!streamOffset)
            read_stream();
        //決定bit是0或1，並決定下一個節點
        m = ((stream&streamOffset)& 0xFF)? 1: 0;
        current = current->node[m];
        streamOffset >>=1;
    }
    return current->val;
}
/***
透過tree_build建立所需要的huffman tree
***/
inline void build_huffman_tree(){
    macroBlockIncremental = tree_build( 33, macroBlockIncrementalData);
    macroBlockType[0] = tree_build( 2, macroBlockTypeIData);
    macroBlockType[1] = tree_build( 7, macroBlockTypePData);
    macroBlockType[2] = tree_build( 11, macroBlockTypeBData);
    macroBlockType[3] = tree_build( 1, macroBlockTypeDData);
    macroBlockMotion = tree_build( 33, macroBlockMotionData);
    macroBlockPattern = tree_build( 63, macroBlockPatternData);
    dctDCLuminanceSize = tree_build( 9, dctDCLuminanceSizeData);
    dctDCChrominanceSize = tree_build( 9, dctDCChrominanceSizeData);
    dctCoefficientFirst = tree_build( 113, dctCoefficientFirstData);
    dctCoefficientNext = tree_build( 113, dctCoefficientNextData);
}

/***
    block process part
***/

/***
處理intra type macroblock，並進行Inverse DET 解碼
***/
inline void decode_intra_block(){  
    //根據spec還原每個block數值
    for( int k=0; k<_BLOCK_IN_MAC; k++){ 
        for( int i=1; i<_BLOCK_SIZE2; i++){
            blockRecon[_CURRENT][k][i] = ( 2 *( block[k][zigZag[i]] * quantizerScale * seq.intraQuantizer[i]) >> 4);
            if( (blockRecon[_CURRENT][k][i]&1) == 0)
                blockRecon[_CURRENT][k][i] -= sign(blockRecon[_CURRENT][k][i]);
            if( blockRecon[_CURRENT][k][i]>2047) 
                blockRecon[_CURRENT][k][i] = 2047;
            else if( blockRecon[_CURRENT][k][i]<-2048) 
                blockRecon[_CURRENT][k][i] = -2048;
        }
        blockRecon[_CURRENT][k][0] = block[k][0] << 3;
    }
    //如果與上一個intra block的位置差距>1，則重置前一個DC value變數
    if( macroBlockAddress - prevIntraAddress > 1){
        blockRecon[_CURRENT][0][0] += 1024; 
        blockRecon[_CURRENT][4][0] += 1024;
        blockRecon[_CURRENT][5][0] += 1024;
    }
    //反之則加上前一個macroblock DC value的值
    else {
        blockRecon[_CURRENT][0][0] += prevDCValue[0];
        blockRecon[_CURRENT][4][0] += prevDCValue[1];
        blockRecon[_CURRENT][5][0] += prevDCValue[2];
    }
    
    //用來計算 previous DC value for Y
    for( int k=1; k<4; k++)
        blockRecon[_CURRENT][k][0] += blockRecon[_CURRENT][k-1][0];

    //set previous value
    prevDCValue[0] = blockRecon[_CURRENT][3][0];
    prevDCValue[1] = blockRecon[_CURRENT][4][0];
    prevDCValue[2] = blockRecon[_CURRENT][5][0];
    prevIntraAddress = macroBlockAddress; 
    
    //進行IDCT處理
    for( int k=0; k<6; k++)
        idct_2d(blockRecon[_CURRENT][k]);
}
/***
處理pattern type macroblock，並進行Inverse DET 解碼
***/
inline void decode_pattern_block(){
    //還原block數值
    for( int k=0; k<6; k++){
        //如果沒有block[i]資料則忽略掉
        if( (mac.pattern & (0x20 >> k)) == 0)
            ;
        else{
            //其餘則還原數值
            for( int i=0; i<_BLOCK_SIZE2; i++){
                int s = zigZag[i];
                blockRecon[_CURRENT][k][i] = ((2*block[k][s]+sign(block[k][s])) * quantizerScale * seq.nonIntraQuantizer[i]) >> 4;
                if( (blockRecon[_CURRENT][k][i]&1)==0)
                    blockRecon[_CURRENT][k][i] -= sign(blockRecon[_CURRENT][k][i]);
                if( blockRecon[_CURRENT][k][i] > 2047)
                    blockRecon[_CURRENT][k][i] = 2047;
                else if(  blockRecon[_CURRENT][k][i] < -2048)
                    blockRecon[_CURRENT][k][i] = -2048;
            }
            idct_2d(blockRecon[_CURRENT][k]);
        }
    } 
}
/***
計算motion vecotr參照的位置座標，根據dir來決定處理forward或是backward motion vertor
***/
inline void calculate_motion( int dir){
    int complement, little, big;
    int min = -16*pic.code[dir], max = -min-1; 
    //計算 right_for(i=0) 和 down_for(i=1)
    for( int i=0; i<2; i++){
        //計算complement數值
        complement = ( pic.code[dir]==1 || !mac.code[dir][i])? 0: pic.code[dir] - 1 - mac.motion[dir][i];
        little = mac.code[dir][i] * pic.code[dir];
        //little = 0, big = 0
        if(!little)
            big = 0;
        else{
            //處理little big數值
            if( little>0){
                little -= complement;
                big = little - 32 * pic.code[dir];
            }
            else{
                little += complement;
                big = little + 32 * pic.code[dir];
            }
        }
        int vector = prevMotion[dir][i] + little;
        //如果vector在max和min內，則motion和前一個motion差距為little
        if( (vector <= max) && (vector >= min))
            motion[dir][i] = prevMotion[dir][i] + little;
        //反之差距為big
        else
            motion[dir][i] = prevMotion[dir][i] + big;
        prevMotion[dir][i] = motion[dir][i];
        //如果 picture的與dir相對應full pel為1，則motion*2
        if(pic.fullPel[dir])
            motion[dir][i] <<= 1;
    }
}
/***
根據motion vecotr參照的位置座標和dir,決定從forward frame或是backward frame搬資料
***/
inline void decode_motion_block(int dir){
    //right(horizontal) down(vertical)
    int lineY = seq.macroSize[0]<<4, lineO = seq.macroSize[0]<<3;
    int pos[2] = { macroBlockAddress%seq.macroSize[0], macroBlockAddress/seq.macroSize[0]};
    int half[2] = { prevMotion[dir][_RIGHT]&0x1, prevMotion[dir][_DOWN]&0x1};
    int divide = half[0]+half[1];   
    motion[dir][0] = prevMotion[dir][0] >> 1;
    motion[dir][1] = prevMotion[dir][1] >> 1;
    //build blockRecon Y，搬運refernce picture的Y值資料
    for( int y=0; y<8; y++){
        for( int x=0; x<8; x++){
            blockRecon[dir][0][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN])*lineY+(pos[0]*16+x+motion[dir][_RIGHT])];
            blockRecon[dir][1][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN])*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT])];
            blockRecon[dir][2][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN])*lineY+(pos[0]*16+x+motion[dir][_RIGHT])];
            blockRecon[dir][3][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN])*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT])];
            //決定是否參考右邊鄰居
            if( half[_RIGHT]){
                blockRecon[dir][0][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN])*lineY+(pos[0]*16+x+motion[dir][_RIGHT]+1)];
                blockRecon[dir][1][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN])*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT]+1)];
                blockRecon[dir][2][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN])*lineY+(pos[0]*16+x+motion[dir][_RIGHT]+1)];
                blockRecon[dir][3][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN])*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT]+1)];
            }
            //決定是否參考下面鄰居
            if( half[_DOWN]){
                blockRecon[dir][0][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+motion[dir][_RIGHT])];
                blockRecon[dir][1][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT])];
                blockRecon[dir][2][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+motion[dir][_RIGHT])];
                blockRecon[dir][3][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT])];            
            }
            //決定是否參考右下鄰居
            if( half[_RIGHT] & half[_DOWN]){
                blockRecon[dir][0][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+motion[dir][_RIGHT]+1)];
                blockRecon[dir][1][y*8+x] += pictureY[dir][(pos[1]*16+y+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT]+1)];
                blockRecon[dir][2][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+motion[dir][_RIGHT]+1)];
                blockRecon[dir][3][y*8+x] += pictureY[dir][(pos[1]*16+y+8+motion[dir][_DOWN]+1)*lineY+(pos[0]*16+x+8+motion[dir][_RIGHT]+1)];
            }
            blockRecon[dir][0][y*8+x] >>= divide;
            blockRecon[dir][1][y*8+x] >>= divide;
            blockRecon[dir][2][y*8+x] >>= divide;
            blockRecon[dir][3][y*8+x] >>= divide;
        }
    }
    //build pel Cb Cr, 搬運refernce picture的Cb Cr值資料
    half[0] = motion[dir][0] & 0x1; 
    half[1] = motion[dir][1] & 0x1;
    divide = half[0] + half[1];
    motion[dir][0] >>= 1;
    motion[dir][1] >>= 1;
    for( int y=0; y<8; y++){
        for( int x=0; x<8; x++){
            blockRecon[dir][4][y*8+x] += pictureCb[dir][(pos[1]*8+y+motion[dir][_DOWN])*lineO+(pos[0]*8+x+motion[dir][_RIGHT])];
            blockRecon[dir][5][y*8+x] += pictureCr[dir][(pos[1]*8+y+motion[dir][_DOWN])*lineO+(pos[0]*8+x+motion[dir][_RIGHT])];
            //決定是否參考右邊鄰居
            if( half[_RIGHT]){
                blockRecon[dir][4][y*8+x] += pictureCb[dir][(pos[1]*8+y+motion[dir][_DOWN])*lineO+(pos[0]*8+x+motion[dir][_RIGHT]+1)];
                blockRecon[dir][5][y*8+x] += pictureCr[dir][(pos[1]*8+y+motion[dir][_DOWN])*lineO+(pos[0]*8+x+motion[dir][_RIGHT]+1)];
            }
            //決定是否參考下面鄰居
            if( half[_DOWN]){
                blockRecon[dir][4][y*8+x] += pictureCb[dir][(pos[1]*8+y+motion[dir][_DOWN]+1)*lineO+(pos[0]*8+x+motion[dir][_RIGHT])];
                blockRecon[dir][5][y*8+x] += pictureCr[dir][(pos[1]*8+y+motion[dir][_DOWN]+1)*lineO+(pos[0]*8+x+motion[dir][_RIGHT])];
            }
            //決定是否參考右下鄰居
            if( half[_RIGHT] && half[_DOWN]){
                blockRecon[dir][4][y*8+x] += pictureCb[dir][(pos[1]*8+y+motion[dir][_DOWN]+1)*lineO+(pos[0]*8+x+motion[dir][_RIGHT]+1)];
                blockRecon[dir][5][y*8+x] += pictureCr[dir][(pos[1]*8+y+motion[dir][_DOWN]+1)*lineO+(pos[0]*8+x+motion[dir][_RIGHT]+1)];
            }
            blockRecon[dir][4][y*8+x] >>= divide;
            blockRecon[dir][5][y*8+x] >>= divide;
        }
    }
}
/***
重設motion vecotr參照的位置座標，根據dir決定重設backward或是forward。
***/
inline void reset_motion(int dir){
    prevMotion[dir][_RIGHT] = 0;
    prevMotion[dir][_DOWN] = 0;
}
/***
將macroblock的資料搬到picture上，好進行RGB解碼
***/
inline void block_to_picture(){
    int pos[2] = { macroBlockAddress%seq.macroSize[0], macroBlockAddress/seq.macroSize[0]}; // x,y
    int lineY = seq.macroSize[0]<<4, lineO = seq.macroSize[0]<<3;
    for( int y=0; y<8; y++){
        for( int x=0; x<8; x++){
            pictureY[_CURRENT][(pos[1]*16+y)*lineY + (pos[0]*16+x)] = (blockRecon[_CURRENT][0][y*8+x]);
            pictureY[_CURRENT][(pos[1]*16+y)*lineY + (pos[0]*16+8+x)] = (blockRecon[_CURRENT][1][y*8+x]);
            pictureY[_CURRENT][(pos[1]*16+8+y)*lineY + (pos[0]*16+x)] = (blockRecon[_CURRENT][2][y*8+x]);
            pictureY[_CURRENT][(pos[1]*16+8+y)*lineY + (pos[0]*16+8+x)] = (blockRecon[_CURRENT][3][y*8+x]);
            pictureCb[_CURRENT][(pos[1]*8+y)*lineO + (pos[0]*8+x)] = (blockRecon[_CURRENT][4][y*8+x]);
            pictureCr[_CURRENT][(pos[1]*8+y)*lineO + (pos[0]*8+x)] = (blockRecon[_CURRENT][5][y*8+x]);
        }
    }
}
/***
將forward motion, backward motion, pattern所取得的資料合在一起
***/
inline void combine_blockRecon(){
    int divide = prevMacroType[_FORWARD]+prevMacroType[_BACKWARD]-1;
    //表示沒有motion data不用合併
    if( divide==-1)
        return;
    for( int k=0; k<_BLOCK_IN_MAC; k++){
        for( int i=0; i<_BLOCK_SIZE2; i++){
            //如果有參照到forward picture
            if( prevMacroType[_FORWARD])
                blockRecon[_CURRENT][k][i] += (blockRecon[_FORWARD][k][i] >> divide);
            //如果有參照到backward picture
            if( prevMacroType[_BACKWARD])
                blockRecon[_CURRENT][k][i] += (blockRecon[_BACKWARD][k][i] >> divide);
        }
    }
}
/***
用來處理three frame buffer的資料交換，以節省開設記憶體的時間
***/
inline void swap_picture(int order1, int order2){
    std::swap( pictureY[order1], pictureY[order2]);
    std::swap( pictureCb[order1], pictureCb[order2]);
    std::swap( pictureCr[order1], pictureCr[order2]);
    std::swap( pictureNo[order1], pictureNo[order2]);
}
/***
將一個frame Y Cb Cr整合成一個RGB資料
根據order決定是forward backward 還是 current需要解碼
***/
inline void ycbcr_to_bgr(int order){
    int size[2] = {seq.macroSize[0]<<4, seq.macroSize[1]<<4}; 
    //解碼的picture no必須是正數，如果不是表示是空資料不做任何處理
    if( pictureNo[order]<=0) return;
    //將三個Y Cb Cr mapping到一個 rgb陣列
    for( int y=0; y<size[1]; y++){
        for( int x=0; x<size[0]; x++){
            int pos[2] = {y*size[0]+x, (y>>1)*(size[0]>>1)+(x>>1)};  // Y Cb&Cr
            pictureRGB[pos[0]*3+_B] = (pictureY[order][pos[0]]-16) + 1.77200 * (pictureCb[order][pos[1]]-128);//b
            pictureRGB[pos[0]*3+_G] = (pictureY[order][pos[0]]-16) - 0.34414 * (pictureCr[order][pos[1]]-128)
                                  - 0.71414 * (pictureCb[order][pos[1]]-128); //g
            pictureRGB[pos[0]*3+_R] = (pictureY[order][pos[0]]-16) + 1.40200 * (pictureCr[order][pos[1]]-128);//r
        }
    }
    //做rgb數值調整，並調整Y軸順序 (因為openGL pixel index從下往上)
    for( int y=0; y<size[1]; y++)
        for( int x=0; x<size[0]*3; x++)
            picture[(size[1]-1-y)*size[0]*3+x] = (unsigned char)((pictureRGB[y*size[0]*3+x]>255)? 255: 
                (pictureRGB[y*size[0]*3+x]<0)? 0: pictureRGB[y*size[0]*3+x]);
}
/***
輸出為bmp檔進行debug，目前沒用
***/
inline void print_picture( int order){ //this part borrow from openstack
    if( pictureNo[order]<=0) return;
    ycbcr_to_bgr(order);
    char path[1000];
    snprintf( path, sizeof(path), "./pic/%d.bmp", ++displayCounter);
    FILE *file = fopen( path, "wb");
	typedef struct                       /**** BMP file header structure ****/
    {
        unsigned int   bfSize;           /* Size of file */
        unsigned short bfReserved1;      /* Reserved */
        unsigned short bfReserved2;      /* ... */
        unsigned int   bfOffBits;        /* Offset to bitmap data */
    } BITMAPFILEHEADER;

    typedef struct                       /**** BMP file info structure ****/
    {
        unsigned int   biSize;           /* Size of info header */
        int            biWidth;          /* Width of image */
        int            biHeight;         /* Height of image */
        unsigned short biPlanes;         /* Number of color planes */
        unsigned short biBitCount;       /* Number of bits per pixel */
        unsigned int   biCompression;    /* Type of compression to use */
        unsigned int   biSizeImage;      /* Size of image data */
        int            biXPelsPerMeter;  /* X pixels per meter */
        int            biYPelsPerMeter;  /* Y pixels per meter */
        unsigned int   biClrUsed;        /* Number of colors used */
        unsigned int   biClrImportant;   /* Number of important colors */
    } BITMAPINFOHEADER;

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;

    /* Magic number for file. It does not fit in the header structure due to alignment requirements, so put it outside */
    unsigned short bfType=0x4d42;           
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfSize = 2+sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)+seq.size[0]*seq.size[1]*3;
    bfh.bfOffBits = 0x36;

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = seq.size[0];
    bih.biHeight = seq.size[1];
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = 0;
    bih.biSizeImage = 0;
    bih.biXPelsPerMeter = 5000;
    bih.biYPelsPerMeter = 5000;
    bih.biClrUsed = 0;
    bih.biClrImportant = 0;
    
    int width = (((seq.size[0]*3+3)>>2)<<2); 
    int height = seq.size[1];
    
    fwrite( &bfType,1,sizeof(bfType), file);
	fwrite( &bfh, 1, sizeof(BITMAPFILEHEADER), file);
    fwrite( &bih, 1, sizeof(BITMAPINFOHEADER), file);
    //輸出bmp rgb資料
    for( int y=0; y<height; y--)
       fwrite( picture+(y*seq.macroSize[0]*16*3), width, sizeof(unsigned char), file);
    fclose(file);
}
/***
根據spec處理skip macroblock
P - 搬運 forward frame資料， motion為0,0
B - 根據上一個macro type和motion vector決定如何搬運
***/
inline void skip_block(){
    if( pic.codingType==_PICTURE_CODING_TYPE_P){
        reset_motion(_FORWARD);
        prevMacroType[_FORWARD] = 1;
    }
    
    if( prevMacroType[_FORWARD])
        decode_motion_block(_FORWARD);
    if( prevMacroType[_BACKWARD])
        decode_motion_block(_BACKWARD);
    combine_blockRecon();
    block_to_picture();
}
/***
將ycbcr_to_bgr產生的picture陣列資料存到decodedFrame。
讓GUI可以顯示資料
***/
inline void save_picture(int order){
    picture = (unsigned char*) malloc( 256*seq.macroSize[0]*seq.macroSize[1]*sizeof(unsigned char)*3);
    //picture NO <=0代表無用資料
    if( picture[order]<=0)
        return;
    ycbcr_to_bgr(order);
    decodedFrame.push_back(picture);
    picture = 0;
}

/***
IDCT part
***/

/***
一維IDCT，參考自halicery.com/Image/idct.html，加快IDCT解碼速度
xmul和 xadd3 在macro宣告 line 41 42
***/
inline void idct_1d( int *x, int *y, int ps, int half){
    int p, n;
    x[0]<<=9, x[1]<<=7, x[3]*=181, x[4]<<=9, x[5]*=181, x[7]<<=7;
    xmul(x[6],x[2],277,669,0);
    xadd3(x[0],x[4],x[6],x[2],half);
    xadd3(x[1],x[7],x[3],x[5],0);
    xmul(x[5],x[3],251,50,6);
    xmul(x[1],x[7],213,142,6);
    y[0]=(x[0]+x[1])>>ps;
    y[8]=(x[4]+x[5])>>ps;
    y[16]=(x[2]+x[3])>>ps;
    y[24]=(x[6]+x[7])>>ps;
    y[32]=(x[6]-x[7])>>ps;
    y[40]=(x[2]-x[3])>>ps;
    y[48]=(x[4]-x[5])>>ps;
    y[56]=(x[0]-x[1])>>ps;
}

//二維IDCT，分別對row和column做IDCT
inline void idct_2d(int *b){
    int b2[64]={0};
    for ( int i=0; i<8; i++) idct_1d(b+i*8, b2+i, 9, 1<<8); // row
    for ( int i=0; i<8; i++) idct_1d(b2+i*8, b+i, 12, 1<<11); // col 
}

/***
GUI part
***/

void keyboard(unsigned char key, int x, int y);
void display();
void frameCalculate(int);

int windowId, subWindowId;
int windowSize[2] = {320, 240};
int frameId = 0, nextFrame = 1, play=1, fpsScale = 4;
float fps = 30.0;
unsigned int mspf;
char titleName[1000] = {};

/***
開啟視窗並顯示Mpeg.1解碼後資料
***/
int gui( int argc, char *argv[]){
  while( decodedFrame.size()<10)
    ;
  mspf = (unsigned int)1000.0/(fps*0.25*fpsScale);
  glutInit( &argc, argv);
  glutInitWindowSize( windowSize[0], windowSize[1]);
  snprintf( titleName, 1000, "fps %.2f", 0.25*fpsScale);
  glutCreateWindow( titleName);
  glutKeyboardFunc( keyboard);
  glutDisplayFunc( display);
  glutMainLoop();
  return EXIT_SUCCESS;
}
/***
偵測鍵盤按鈕
操作方式放在report
***/
void keyboard(unsigned char key, int x, int y){
    switch (key){
        case 'r':
            nextFrame = 1;
            frameId = -1;
            break;
        case 'e':
            frameId = decodedFrame.size()-1;
            break;
        case 's':
            fpsScale--;
            if( fpsScale < 1)
                fpsScale = 1;
            mspf = (unsigned int)1000.0/(fps*0.25*fpsScale);
            break;
        case 'f':
            fpsScale++;
            if( fpsScale > 12)
                fpsScale = 12;
            mspf = (unsigned int)1000.0/(fps*0.25*fpsScale);
            break;
        case 'p':
            play ^= 0x1;
            break;
        case 'b':
            nextFrame = -nextFrame;
            break;
    }
    if( play)
        snprintf( titleName, 1000, "speed %.2f", 0.25*fpsScale*nextFrame);
    else
        snprintf( titleName, 1000, "speed %.2f pause", 0.25*fpsScale*nextFrame);
    glutSetWindowTitle(titleName);
}
/***
列印一個frame的資料
***/
void display(){
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawPixels( seq.macroSize[0]<<4, seq.macroSize[1]<<4, GL_RGB, GL_UNSIGNED_BYTE, decodedFrame[frameId]);
    glutSwapBuffers();
    glFlush();
    glutTimerFunc( mspf, frameCalculate, 0);
}
/***
等 mspf毫秒來決定下一個frame並display該frame
***/
void frameCalculate(int value){
    if( play)
        frameId += (nextFrame);
    if( frameId < 0 )
        frameId = 0;
    else if( frameId >= decodedFrame.size())
        frameId = decodedFrame.size()-1;
    glutPostRedisplay();
}