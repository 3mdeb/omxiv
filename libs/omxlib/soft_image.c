/* Copyright (c) 2015, Benjamin Huber
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the copyright holder nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <setjmp.h>

#include <jpeglib.h>

#include "soft_image.h"

#define ALIGN16(x) (((x+0xf)>>4)<<4)

#define MIN_FRAME_DELAY_CS 2
#define BUMP_UP_FRAME_DELAY_CS 10

static const char magExif[] = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};

// https://stackoverflow.com/questions/19857766/error-handling-in-libjpeg
struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr * my_error_ptr;

METHODDEF(void) my_error_exit (j_common_ptr cinfo) {
	my_error_ptr myerr = (my_error_ptr) cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}


int readJpegHeader(FILE *infile, JPEG_INFO *jpegInfo){
	struct jpeg_decompress_struct cinfo;
	
	struct my_error_mgr jerr;

	if(infile == NULL){
		return SOFT_IMAGE_ERROR_FILE_OPEN; 
	}

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return SOFT_IMAGE_ERROR_DECODING;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);
	jpeg_save_markers(&cinfo, JPEG_APP0+1, 0xffff);
	jpeg_read_header(&cinfo, TRUE);
	
	if(cinfo.progressive_mode)
		jpegInfo->mode = JPEG_MODE_PROGRESSIVE;
	else
		jpegInfo->mode = JPEG_MODE_NON_PROGRESSIVE;
		
	jpegInfo->nColorComponents = cinfo.num_components;
	
	
	// read EXIF orientation
	// losely base on: http://sylvana.net/jpegcrop/jpegexiforient.c
	jpegInfo->orientation = 1; // Default
	jpeg_saved_marker_ptr pMarker = cinfo.marker_list;
	
	if(pMarker != NULL && pMarker->data_length >= 20 && 
		memcmp(pMarker->data, magExif, sizeof(magExif)) == 0) {
			
		unsigned int exifLen = pMarker->data_length;
		uint8_t* exifData = pMarker->data;
		short motorola;
		
		// byte order 
		if(exifData[6] == 0x49 && exifData[7] == 0x49)
			motorola = 0;
		else if(exifData[6] == 0x4D && exifData[7] == 0x4D)
			motorola = 1;
		else
			goto cleanExit;

		if (motorola) {
			if(exifData[8] != 0 || exifData[9] != 0x2A) 
				goto cleanExit;
		} else {
			if(exifData[9] != 0 || exifData[8] != 0x2A) 
				goto cleanExit;
		}
		
		unsigned int offset;
		// read offset to IFD0
		if(motorola) {
			if (exifData[10] != 0 || exifData[11] != 0)
				goto cleanExit;
			offset = (exifData[12]<<8) + exifData[13] + 6;
		} else {
			if (exifData[12] != 0 || exifData[13] != 0)
				goto cleanExit;
			offset = (exifData[11]<<8) + exifData[10] + 6;
		}
		if(offset > exifLen - 14)
			goto cleanExit;
		
		unsigned int nTags;
		
		// read number of tags in IFD0
		if(motorola)
			nTags = (exifData[offset]<<8) + exifData[offset+1];
		else 
			nTags = (exifData[offset+1]<<8) + exifData[offset];

		offset += 2;

		while(1) {
			if (nTags-- == 0 || offset > exifLen - 12)
				goto cleanExit;
			
			unsigned int tag;
			if (motorola)
				tag = (exifData[offset]<<8) + exifData[offset+1];
			else 
				tag = (exifData[offset+1]<<8) + exifData[offset];
				
			if (tag == 0x0112) break; // orientation tag found
			
			offset += 12;
		}
		
		unsigned char orientation = 9;
		
		if (motorola && exifData[offset+8] == 0) {
			orientation = exifData[offset+9];
		} else if(exifData[offset+9] == 0) {
			orientation = exifData[offset+8];
		}
		
		if (orientation <= 8 && orientation != 0)
			jpegInfo->orientation = orientation;
	}
	
cleanExit:	
	
	jpeg_destroy_decompress(&cinfo);
	
	return SOFT_IMAGE_OK;
}

int softDecodeJpeg(FILE *infile, IMAGE *jpeg){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPARRAY buffer;
	unsigned int rowStride;
	
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return SOFT_IMAGE_ERROR_DECODING;
	}
	
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);
	jpeg_read_header(&cinfo, TRUE);
	
	cinfo.out_color_space = JCS_RGB;
	
	jpeg_start_decompress(&cinfo);
	
	rowStride = cinfo.output_width * cinfo.output_components;
	
	jpeg->width = cinfo.output_width;
	
	/* Stride memory needs to be a multiple of 16, 
	 * otherwise resize and render component will bug. */
	unsigned int stride = ALIGN16(jpeg->width)*4;
	
	jpeg->height = cinfo.output_height;
	jpeg->colorSpace = COLOR_SPACE_RGBA;
	
	buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, rowStride, 1);
	size_t i, x,y;
	
	jpeg->nData = stride * ALIGN16(cinfo.output_height);
	jpeg->pData = malloc(jpeg->nData);
	if(jpeg->pData == NULL){
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return SOFT_IMAGE_ERROR_MEMORY;
	}
	
	
	size_t rBytes= cinfo.output_width*4;
	
	// Copy and convert from RGB to RGBA
	for(i=0; cinfo.output_scanline < cinfo.output_height; i+=stride) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		for(x = 0,y=0; x < rBytes; x+=4, y+=3){
			jpeg->pData[i+x+3]=255;
			memcpy(jpeg->pData+i+x, buffer[0]+y, 3);
		}
	}
	
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	
	if(jerr.pub.num_warnings != 0){
		return SOFT_IMAGE_ERROR_CORRUPT_DATA;
	}
	
	return SOFT_IMAGE_OK;
}

struct MemoryStruct {
	unsigned char *memory;
	size_t size;
};

