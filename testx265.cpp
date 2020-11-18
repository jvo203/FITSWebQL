#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <x265.h>

#define CELLSIZE 4

int main(int argc, char **argv)
{
    x265_nal *pNals = NULL;
    uint32_t iNal = 0;

    x265_param *param = NULL;
    x265_encoder *encoder = NULL;
    x265_picture *picture = NULL;

    int img_width = 601;
    int img_height = 601;

    int padded_width = img_width;
    if (img_width % CELLSIZE > 0)
        padded_width += CELLSIZE - img_width % CELLSIZE;

    int padded_height = img_height;
    if (img_height % CELLSIZE > 0)
        padded_height += CELLSIZE - img_height % CELLSIZE;

    printf("img: %d x %d; padded img: %d x %d\n", img_width, img_height, padded_width, padded_height);

    param = x265_param_alloc();
    //x265_param_default(param);
    x265_param_default_preset(param, "medium", "zerolatency");

    param->bRepeatHeaders = 1; //write sps,pps before keyframe
    param->internalCsp = X265_CSP_I444;
    param->sourceWidth = img_width;
    param->sourceHeight = img_height;
    param->fpsNum = 25;
    param->fpsDenom = 1;
    param->internalBitDepth = 8;

    // constant bitrate
    param->rc.rateControlMode = X265_RC_CRF;
    param->rc.bitrate = 1000;

    //Init
    encoder = x265_encoder_open(param);
    if (encoder == NULL)
    {
        printf("x265_encoder_open err\n");
        return 0;
    }

    // padded R,G,B channels
    const size_t frame_size = padded_width * padded_height;

    uint8_t *R_buf = (uint8_t *)malloc(frame_size);
    uint8_t *G_buf = (uint8_t *)malloc(frame_size);
    uint8_t *B_buf = (uint8_t *)malloc(frame_size);

    /*memset(R_buf, 100, frame_size);
    memset(G_buf, 150, frame_size);
    memset(B_buf, 200, frame_size);*/

    memset(R_buf, 100, frame_size);
    memset(G_buf, 255, frame_size);
    memset(B_buf, 0, frame_size);

    picture = x265_picture_alloc();
    x265_picture_init(param, picture);

    picture->planes[0] = R_buf;
    picture->planes[1] = G_buf;
    picture->planes[2] = B_buf;

    picture->stride[0] = padded_width;
    picture->stride[1] = padded_width;
    picture->stride[2] = padded_width;

    for (int i = 0; i < 10; i++)
    {
        int ret = x265_encoder_encode(encoder, &pNals, &iNal, picture, NULL);
        printf("encoding frame %d; ret = %d; nal count = %d\n", i, ret, iNal);
    }

    x265_encoder_close(encoder);
    x265_picture_free(picture);
    x265_param_free(param);

    free(R_buf);
    free(G_buf);
    free(B_buf);

    return 0;
}