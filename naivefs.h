#define MAX_FILENAME_LEN 8
#define MAX_FILES 128
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
        size_t file_size;
        size_t dir_children;
    };
    char data[NAIVE_FS_BLOCK_SIZE];
    uint8_t next_block_idx;
};