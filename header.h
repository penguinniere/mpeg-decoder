#define _HORIZONTAL  0x0
#define _VERTICAL 0x1
#define _BACKWARD 0x0
#define _FORWARD 0x1
#define _CURRENT 0x2
#define _RIGHT 0x0
#define _DOWN 0x1
#define _PICTURE_CODING_TYPE_I 0x1
#define _PICTURE_CODING_TYPE_P 0x2
#define _PICTURE_CODING_TYPE_B 0x3
#define _PICTURE_CODING_TYPE_D 0x4
//#define _Y 0x0
//#define _O 0x1
//define sequence layer headder
struct Seq{
    int size[2];  //x y
    int macroSize[2];// x y
    int pelAspectRatio;
    int pictureRate;
    int bitRate;
    int vbvBufferSize;
    int constrain;
    int intraQuantizer[64];
    int nonIntraQuantizer[64];
};typedef struct Seq Seq;
//define group of picture layer header

struct Gop{
    int dropGrame;
    int time;
    int closed;
    int broken;
};typedef struct Gop Gop;

//define picture layer header
struct Pic{
    int temperal;
    int codingType;
    int vbvDelay;
    int fullPel[2]; //backward forward
    int code[2]; //backward forward
    int size[2];
    //int extraInformation
};typedef struct Pic Pic;

//define slice layer header
struct Sli{
    int vertical;
    //int quantizerScale;
    //int extraInformation;
};typedef struct Sli Sli;

//define macroblock layer header
struct Mac{
    //int increment;
    int type;
    int code[2][2];  //motion_forward_code [backward forward] [right down]
    int motion[2][2];//[backward forward] [right down]
    int pattern;  
    //int quantizerScale;
};typedef struct Mac Mac;

//define pixel
struct Pixel{
    char r;
    char g;
    char b;
};typedef struct Pixel Pixel;

//define decoded picture
/*
struct Decoded{
    int hight;
    int width;
    int *data;
}; typedef struct Decoded Dec;*/

int defaultIntraQuantizer[64] = {
8, 16, 19, 22, 26, 27, 29, 34,
16, 16, 22, 24, 27, 29, 34, 37,
19, 22, 26, 27, 29, 34, 34, 38,
22, 22, 26, 27, 29, 34, 37, 40,
22, 26, 27, 29, 32, 35, 40, 48,
26, 27, 29, 32, 35, 40, 48, 58,
26, 27, 29, 34, 38, 46, 56, 69,
27, 29, 35, 38, 46, 56, 69, 83
};

int defaultNonIntraQuantizer[64] ={
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16,
16, 16, 16, 16, 16, 16, 16, 16
};  

int zigZag[64] = {
0,  1,  5,  6, 14, 15, 27, 28,
2,  4,  7, 13, 16, 26, 29, 42,
3,  8, 12, 17, 25, 30, 41, 43,
9, 11, 18, 24, 31, 40, 44, 53,
10, 19, 23, 32, 39, 45, 52, 54,
20, 22, 33, 38, 46, 51, 55, 60,
21, 34, 37, 47, 50, 56, 59, 61,
35, 36, 48, 49, 57, 58, 62, 63
};