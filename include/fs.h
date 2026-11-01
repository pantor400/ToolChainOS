#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

#define FS_MAX_NODES 64
#define FS_NAME_MAX 32
#define FS_FILE_MAX_SIZE 1024
#define FS_PATH_MAX 128

typedef enum {
    FS_TYPE_FREE = 0,
    FS_TYPE_DIR,
    FS_TYPE_FILE
} fs_node_type_t;

typedef struct fs_node {
    fs_node_type_t type;
    char name[FS_NAME_MAX];
    int parent_idx;
    uint32_t size;
    char content[FS_FILE_MAX_SIZE];
} fs_node_t;

void fs_init(void);
int fs_get_cwd(void);
int fs_set_cwd(int idx);
void fs_get_cwd_path(char *buf, size_t max_len);

int fs_mkdir(const char *dir_path, const char *name, char *err_msg, size_t err_size);
int fs_create_file(const char *dir_path, const char *name, const char *content, char *err_msg, size_t err_size);
int fs_find_node(const char *path);
const char *fs_read_file(const char *path, uint32_t *size_out);
void fs_list_dir(const char *path);

int fs_split(const char *path,char *dir,char *name);
int fs_write_path(const char *path,const char *text,char *err,size_t cap);
int fs_remove(const char *path,int directory,char *err,size_t cap);
int fs_rename(const char *path,const char *name,char *err,size_t cap);
int fs_copy(const char *src,const char *dst,char *err,size_t cap);
void fs_tree(const char *path);
const fs_node_t *fs_nodes(void);
int fs_import(const fs_node_t *nodes);
#endif
