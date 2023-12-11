#define MAX_FILENAME_LEN 8
#define MAX_FILES 32
#define NAIVE_FS_BLOCK_SIZE 512
#define BLOCK_NOT_FOUND -1



struct dir_entry {
    char filename[MAX_FILENAME_LEN];
    uint8_t idx;
};

struct file_blk {
    uint8_t busy; // 指示该块是否被忙
    mode_t mode;
    uint8_t idx;
    union {
        uint8_t file_size;
        uint8_t dir_children;
    };
    char data[512];
};