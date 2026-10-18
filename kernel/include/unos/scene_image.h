#ifndef UNOS_SCENE_IMAGE_H
#define UNOS_SCENE_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/heap.h>

/* Compositor-owned immutable copy of a completed software presentation.
 * The producer may reuse its mutable Surface as soon as presentation is ACKed.
 * Never retain a borrowed producer pointer across that ACK. */
typedef struct scene_image {
    uint32_t *pixels;
    uint32_t width, height;
} scene_image_t;

static inline void scene_image_release(scene_image_t *image) {
    kfree(image->pixels);
    *image=(scene_image_t){0};
}
static inline int scene_image_store(scene_image_t *image,
        const uint32_t *pixels, uint32_t stride, uint32_t width, uint32_t height) {
    if(!pixels || !width || !height || stride<width ||
       (size_t)height>SIZE_MAX/sizeof(uint32_t)/width ||
       (size_t)height>SIZE_MAX/sizeof(uint32_t)/stride)return -1;
    uint32_t *destination=image->pixels;
    if(!destination || image->width!=width || image->height!=height) {
        destination=kmalloc((size_t)width*height*sizeof(uint32_t));
        if(!destination)return -1; /* preserve the previous version on OOM */
    }
    for(uint32_t y=0;y<height;++y)
        memcpy(destination+(size_t)y*width,pixels+(size_t)y*stride,(size_t)width*sizeof(uint32_t));
    if(destination!=image->pixels)kfree(image->pixels);
    *image=(scene_image_t){destination,width,height};
    return 0;
}
#endif
