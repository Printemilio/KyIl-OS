#include <unos/scene_image.h>
extern void *malloc(size_t);
extern void free(void *);
extern void abort(void);
extern int puts(const char *);
extern int printf(const char *,...);
static unsigned allocations, releases;
static int fail;
void *kmalloc(size_t n){if(fail)return NULL;++allocations;return malloc(n);}
void kfree(void *p){if(p){++releases;free(p);}}
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);abort();}}while(0)
int main(void){
    scene_image_t image={0};
    uint32_t pixels[]={1,2,99,3,4,99};
    CHECK(scene_image_store(&image,pixels,3,2,2)==0);
    CHECK(image.pixels[0]==1 && image.pixels[2]==3 && image.pixels[3]==4);
    pixels[0]=42;CHECK(image.pixels[0]==1); /* producer reuses after ACK */
    CHECK(scene_image_store(&image,pixels,3,2,2)==0 && allocations==1);
    uint32_t *previous=image.pixels;fail=1;
    CHECK(scene_image_store(&image,pixels,3,3,2)==-1);
    CHECK(image.pixels==previous && image.width==2 && image.pixels[0]==42);
    CHECK(scene_image_store(&image,pixels,1,2,2)==-1);
    CHECK(scene_image_store(&image,pixels,3,0,2)==-1);
    fail=0;CHECK(scene_image_store(&image,pixels,3,3,2)==0 && releases==1);
    scene_image_release(&image);scene_image_release(&image);
    CHECK(!image.pixels && !image.width && allocations==releases);
    puts("Completed scene image: stride/isolation/reuse/resize/OOM/cleanup PASS");
}
