#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <ghost/alloc.h>
#include <ghost/dynamic_array.h>

typedef struct {
    gh_alloc * alloc;
    char * buffer;
    size_t size;
    size_t capacity;
} expanding_buffer;

static const gh_dynamicarrayoptions expanding_buffer_daopts = {
    .element_size = sizeof(char),
    .initial_capacity = 1,
    .max_capacity = GH_DYNAMICARRAY_NOMAXCAPACITY,
    .dtorelement_func = NULL,
    .userdata = NULL
};

extern const char * lorem_ipsum_text;
extern size_t lorem_ipsum_length;

static int simulate_open_file(void) {
    int fd = memfd_create("lorem-ipsum", MFD_CLOEXEC);
    assert(fd >= 0);

    assert(ftruncate(fd, (off_t)lorem_ipsum_length + 1) == 0);

    ssize_t write_res = write(fd, lorem_ipsum_text, lorem_ipsum_length + 1);
    assert(write_res > 0);
    assert((size_t)write_res == lorem_ipsum_length + 1);

    assert(lseek(fd, 0, SEEK_SET) == 0);

    return fd;
}

static size_t next_pow(size_t num) {
    size_t pow = 1;
    while (pow < num) {
        pow *= 2;
    }
    return pow;
}

int main(void) {
    gh_alloc alloc = gh_alloc_default();

    expanding_buffer buffer;
    buffer.alloc = &alloc;

    ghr_assert(gh_dynamicarray_ctor(GH_DYNAMICARRAY(&buffer), &expanding_buffer_daopts));

    int fd = simulate_open_file();

    char buf[16];
    ssize_t len;
    do {
        len = read(fd, buf, sizeof(buf));
        assert(len >= 0) ;

        ghr_assert(gh_dynamicarray_appendmany(GH_DYNAMICARRAY(&buffer), &expanding_buffer_daopts, buf, (size_t)len));
    } while (len == 16);

    printf("BUFFER CONTENTS:\n");
    printf("%s\n", buffer.buffer);
    assert(strncmp(buffer.buffer, lorem_ipsum_text, lorem_ipsum_length + 1) == 0);

    printf("BUFFER SIZE: %zu\n", buffer.size);
    assert(buffer.size == lorem_ipsum_length + 1);
    printf("BUFFER CAPACITY: %zu\n", buffer.capacity);
    assert(buffer.capacity == next_pow(lorem_ipsum_length + 1));

    // remove null byte
    ghr_assert(gh_dynamicarray_removeat(GH_DYNAMICARRAY(&buffer), &expanding_buffer_daopts, buffer.size - 1));

    // duplicate content
    //
    // we cannot duplicate directly from buffer.buffer, because during appendmany the backing buffer might be reallocated
    // and the old pointer to the backing buffer may become invalid
    char tmp_buf[buffer.size];
    memcpy(tmp_buf, buffer.buffer, buffer.size);
    ghr_assert(gh_dynamicarray_appendmany(GH_DYNAMICARRAY(&buffer), &expanding_buffer_daopts, tmp_buf, buffer.size));

    char z = '\0';
    ghr_assert(gh_dynamicarray_append(GH_DYNAMICARRAY(&buffer), &expanding_buffer_daopts, &z));

    printf("BUFFER CONTENTS:\n");
    printf("%s\n", buffer.buffer);
    printf("BUFFER SIZE: %zu\n", buffer.size);
    assert(buffer.size == lorem_ipsum_length * 2 + 1);
    printf("BUFFER STRLEN: %zu\n", strlen(buffer.buffer));
    assert(strlen(buffer.buffer) == lorem_ipsum_length * 2);
    printf("BUFFER CAPACITY: %zu\n", buffer.capacity);
    assert(buffer.capacity == next_pow(lorem_ipsum_length * 2 + 1));

    ghr_assert(gh_dynamicarray_dtor(GH_DYNAMICARRAY(&buffer), &expanding_buffer_daopts));
    return 0;
}

// Lorem 
size_t lorem_ipsum_length = 3737;
const char * lorem_ipsum_text = "Lorem ipsum odor amet, consectetuer adipiscing elit. Condimentum auctor consectetur eleifend facilisis pretium ad fringilla. Etiam magnis sollicitudin molestie ante iaculis pulvinar enim. Posuere a ultricies laoreet primis fermentum vel sed facilisi. Imperdiet mi diam phasellus suscipit lacinia lacus proin pretium metus. Elementum parturient malesuada phasellus porta massa interdum. Donec nam suscipit dolor lectus senectus cubilia hac.\n\nDis aliquam purus neque nulla tortor lorem egestas cubilia donec. Integer sapien cubilia scelerisque fermentum mus venenatis potenti vivamus eros. Diam aptent gravida nullam; ullamcorper phasellus eleifend. Lobortis placerat platea tempor pretium fringilla quisque. Luctus vel cursus mattis tortor nisl volutpat metus magna. Orci natoque feugiat leo ultricies eleifend. Semper dapibus efficitur feugiat quis; amet aptent. Ante euismod nulla ut imperdiet conubia dolor.\n\nFames diam consequat dictum nullam euismod inceptos. Suscipit odio primis; ante ipsum velit platea praesent. Natoque quam felis congue euismod maximus consectetur. Vehicula accumsan et leo nec sociosqu maecenas montes semper. Vitae donec tempus condimentum suspendisse per sit. Ac phasellus mi eget; faucibus sagittis at morbi nascetur. In ridiculus turpis elit urna quisque dis mi.\n\nJusto ut massa aliquam metus enim. Adipiscing nostra elementum id donec elit metus habitant phasellus est! Morbi maecenas scelerisque ut nascetur cursus laoreet. Parturient ante arcu urna; nibh felis et aptent. Fermentum eu per taciti platea per. Lorem curae montes proin nulla nostra. Arcu vivamus pharetra ac euismod fringilla eu quisque quis nunc. Rhoncus convallis lectus maecenas quam laoreet semper in. Auctor maximus at consequat natoque, dictum netus tempor maecenas arcu.\n\nLorem etiam praesent condimentum posuere odio potenti. Maximus mattis nulla venenatis luctus dui netus. Nam id phasellus ante nullam placerat. Facilisis tempus mollis justo ad ex erat rhoncus tempus amet. Cubilia commodo pellentesque nulla viverra nostra. Dis felis arcu magna dictum tellus. Nostra ad nisl lacinia rutrum accumsan enim turpis.\n\nCongue diam sem bibendum nunc justo. Pulvinar volutpat lectus; torquent felis arcu ornare praesent tempus? Fermentum congue maximus condimentum maximus duis gravida. Felis augue cras sapien quam nulla pulvinar efficitur. Tincidunt sagittis per est mi quisque maecenas per curae. Mattis turpis lobortis vivamus facilisi fringilla inceptos fusce. Pretium consectetur nisl magna morbi nascetur nisi ultricies libero. Curabitur id integer faucibus; id accumsan phasellus suspendisse ac.\n\nFinibus et gravida tincidunt consectetur sapien litora montes. Orci diam dictum quam senectus sollicitudin cursus interdum adipiscing malesuada. Integer class felis dictum cursus ante augue. Dolor pharetra maecenas quam pellentesque leo volutpat tellus. Condimentum tristique arcu curae eu sollicitudin mauris. Congue placerat cras tempor dapibus est? Quisque imperdiet neque magna porttitor, porta sem. Turpis et tempus; pretium orci egestas sociosqu faucibus mus nullam. Lacinia volutpat phasellus lobortis proin blandit dis. Parturient facilisis nec eget turpis taciti vehicula; erat venenatis magna.\n\nRidiculus aliquam habitant feugiat nibh nulla sit facilisi maecenas. Pellentesque torquent dignissim euismod adipiscing rutrum vestibulum habitant semper. Himenaeos magnis iaculis id lacinia sodales justo. Commodo habitant sociosqu dolor fringilla duis risus. Egestas nulla quis habitant dignissim curae pulvinar nisl? Et natoque ut suspendisse lobortis non, mus nostra euismod. Proin porta ultrices risus, vehicula augue torquent scelerisque. Augue habitasse tristique tempus porta tincidunt; gravida euismod erat vulputate.\n";
