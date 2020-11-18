#include <stdio.h>
#include <stdlib.h>

#include <x265.h>

#define CELLSIZE 4

int main(int argc, char **argv)
{
    x265_nal *pNals = NULL;
    uint32_t iNal = 0;

    x265_param *pParam = NULL;
    x265_encoder *pHandle = NULL;
    x265_picture *pPic_in = NULL;

    int img_width = 125;
    int img_height = 75;

    int padded_width = img_width;
    if (img_width % CELLSIZE > 0)
        padded_width += CELLSIZE - img_width % CELLSIZE;

    int padded_height = img_height;
    if (img_height % CELLSIZE > 0)
        padded_height += CELLSIZE - img_height % CELLSIZE;

    printf("img: %d x %d; padded img: %d x %d\n", img_width, img_height, padded_width, padded_height);

    return 0;
}