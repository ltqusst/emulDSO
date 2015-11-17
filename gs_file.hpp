#ifndef _GS_FILE_H_
#define _GS_FILE_H_

#include <stdio.h>
#include <string.h>

struct gs_file
{
    char strFilePath[1024];
    char * pfileName;
    
    char strListDir[1024];


    FILE *inner_fp;
    gs_file() : inner_fp(NULL), pfileName(NULL){ strListDir[0] = 0; };
    gs_file(const char * strfilename) : inner_fp(NULL), pfileName(NULL)
    {
        char drive[_MAX_DRIVE];
        char dir[_MAX_DIR];
        char fname[_MAX_FNAME];
        char ext[_MAX_EXT];

        inner_fp = fopen(strfilename, "rb");
        if (inner_fp == NULL) 
        {
            printf("error in gs_file::gs_file(): cannot open file [%s]\n", strfilename);
            _exit(0);
        }
        //extract path root from the fullpath of listfile
        _splitpath(strfilename, drive, dir, fname, ext); // C4996
        sprintf(strListDir, "%s%s", drive, dir);
    };
    ~gs_file() { if (inner_fp) fclose(inner_fp); };
    
    int next(FILE *fp = NULL)
    {
        if (fp == NULL) fp = inner_fp;
        if (fp == NULL) printf("error in gs_file::next(): no file list!");

        fgets(strFilePath, 1024, fp);
        if (feof(fp)) return 0;

        if (strFilePath[strlen(strFilePath) - 1] == '\n') strFilePath[strlen(strFilePath) - 1] = 0;
        if (strFilePath[strlen(strFilePath) - 1] == '\r') strFilePath[strlen(strFilePath) - 1] = 0;

        pfileName = strrchr(strFilePath, '\\');
        if (pfileName == NULL) 
            printf("error in gs_file::next(): no file name in (%s)!\r\n", strFilePath);
        return 1;
    }

    int next_activity_file(FILE *fp, int &cat_id)
    {
        if (fp == NULL) fp = inner_fp;
        if (fp == NULL) printf("error in gs_file::next(): no file list!");

        fgets(strFilePath, 1024, fp);
        if (feof(fp)) return 0;

        if (strFilePath[strlen(strFilePath) - 1] == '\n') strFilePath[strlen(strFilePath) - 1] = 0;
        if (strFilePath[strlen(strFilePath) - 1] == '\r') strFilePath[strlen(strFilePath) - 1] = 0;

        char * pcat_id = strrchr(strFilePath, ',');
        pcat_id[0] = 0;
        cat_id = atoi(pcat_id + 1);

        pfileName = strrchr(strFilePath, '\\');
        if (pfileName == NULL)
            printf("error in gs_file::next(): no file name in (%s)!\r\n", strFilePath);
        return 1;
    }

};

#define GS_FORMAT_SSV_6T     0
#define GS_FORMAT_CSV_6T     1
#define GS_FORMAT_CSV_3T     2
#define GS_FORMAT_SSV_9TT    3

struct gs_sample
{
    int x, y, z;
    int gx, gy, gz;
    int mx, my, mz;
    int id, id2;
    
    const char *inner_filename;
    FILE *inner_fp;
    int format;
    int line_num;

    float src_delta_T;
    float dst_delta_T;

    float tick;
    float dst_tick;
    float length_sec;

    gs_sample() : inner_fp(NULL){};
    gs_sample(const char * strfilename, int param_format, int param_dst_sample_rate = 100, int param_src_sample_rate = 100, float offset_sec = 0, float end_sec = 999999999) : inner_fp(NULL){
        char buf[1024];
        inner_filename = strfilename;
        inner_fp = fopen(strfilename, "rb");
        if (inner_fp == NULL)
        {
            printf("error in gs_sample::gs_sample(), cannot open (%s)\n", strfilename);
            _exit(0);
        }
        if (GS_FORMAT_SSV_6T != param_format &&
            GS_FORMAT_CSV_6T != param_format &&
            GS_FORMAT_CSV_3T != param_format &&
            GS_FORMAT_SSV_9TT != param_format)
        {
            printf("error in gs_sample::gs_sample(), format undefined(%d)\n", param_format);
            _exit(0);
        }
        format = param_format;
        src_delta_T = 1.0f/param_src_sample_rate;
        dst_delta_T = 1.0f/param_dst_sample_rate;
        tick = dst_tick = 0;
        line_num = 0;

        length_sec = end_sec - offset_sec;
        while (offset_sec > 0)
        {
            fgets(buf, 1024, inner_fp);
            offset_sec -= src_delta_T;
        }
        
    };
    ~gs_sample() { if (inner_fp) fclose(inner_fp); };


    int scanf(void)
    {
        FILE * fp = inner_fp;
        if (fp == NULL) printf("error in gs_sample::scanf(): no file list!");

        dst_tick += dst_delta_T;
        for (; tick < dst_tick; tick += src_delta_T)
        {
            line_num++;
            switch (format)
            {
            case GS_FORMAT_CSV_6T:
                if(7 != fscanf(fp, "%d,%d,%d,%d,%d,%d,%d\r\n", &x, &y, &z, &gx, &gy, &gz, &id))
                {
                    printf("error in gs_sample::scanf(format:%d) : %s:%d\n", format, inner_filename, line_num);
                    _exit(0);
                }
                break;
            case GS_FORMAT_CSV_3T:
                if (3 != fscanf(fp, "%d,%d,%d\r\n", &x, &y, &z))
                {
                    printf("error in gs_sample::scanf(format:%d) : %s:%d\n", format, inner_filename, line_num);
                    _exit(0);
                }
                break;
            case GS_FORMAT_SSV_9TT:
                if (11 != fscanf(fp, "%d %d %d %d %d %d %d %d %d %d %d\r\n", &x, &y, &z, &gx, &gy, &gz, &mx, &my, &mz, &id, &id2))
                {
                    printf("error in gs_sample::scanf(format:%d) : %s:%d\n", format, inner_filename, line_num);
                    _exit(0);
                }
                break;
            case GS_FORMAT_SSV_6T:
            default:
                if (7 != fscanf(fp, "%d %d %d %d %d %d %d\r\n", &x, &y, &z, &gx, &gy, &gz, &id))
                {
                    printf("error in gs_sample::scanf(format:%d) : %s:%d\n",format, inner_filename, line_num);
                    _exit(0);
                }
            }
            length_sec -= src_delta_T;
            if (length_sec < 0) return 0;
            if (feof(fp)) break;
        }
        return !(feof(fp));
    }
};


#endif