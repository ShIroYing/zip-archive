//====================================================================================================
// Copyright (C) 2016-present ShIroRRen <http://shiror.ren>.                                         =
//                                                                                                   =
// Licensed under the F2DLPR License.                                                                =
//                                                                                                   =
// YOU MAY NOT USE THIS FILE EXCEPT IN COMPLIANCE WITH THE LICENSE.                                  =
// Provided "AS IS", WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,                                   =
// unless required by applicable law or agreed to in writing.                                        =
//                                                                                                   =
// For the F2DLPR License terms and conditions, visit: <http://license.fileto.download>.             =
//====================================================================================================

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __GNUC__
#define ZA_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define ZA_INLINE __forceinline
#else
#define ZA_INLINE inline
#endif

#pragma pack(push, 1)

// 局部文件头结构体
typedef struct {
    uint32_t signature;           // 本地文件头签名（0x04034b50）
    uint16_t version_needed;      // 解压所需的最低版本
    uint16_t general_purpose;     // 通用位标志
    uint16_t compression_method;  // 压缩方法（0表示存档，不压缩）
    uint16_t last_mod_time;       // 最后修改时间
    uint16_t last_mod_date;       // 最后修改日期
    uint32_t crc32;               // CRC-32校验值
    uint32_t compressed_size;     // 压缩后大小
    uint32_t uncompressed_size;   // 未压缩大小
    uint16_t filename_length;     // 文件名长度
    uint16_t extra_field_length;  // 扩展字段长度（ALL 2 0）
} ZALocalFileHeader;

// 中央目录文件头结构体
typedef struct {
    uint32_t signature;            // 中央目录文件头签名（0x02014b50）
    uint16_t version_made_by;      // 版本信息
    uint16_t version_needed;       // 解压所需的最低版本
    uint16_t general_purpose;      // 通用位标志
    uint16_t compression_method;   // 压缩方法（0表示存档）
    uint16_t last_mod_time;        // 最后修改时间
    uint16_t last_mod_date;        // 最后修改日期
    uint32_t crc32;                // CRC-32校验值
    uint32_t compressed_size;      // 压缩后大小
    uint32_t uncompressed_size;    // 未压缩大小
    uint16_t filename_length;      // 文件名长度
    uint16_t extra_field_length;   // 扩展字段长度（ALL 2 0）
    uint16_t file_comment_length;  // 文件注释长度（ALL 2 0）
    uint16_t disk_number_start;    // 起始磁盘号
    uint16_t internal_attributes;  // 内部文件属性
    uint32_t external_attributes;  // 外部文件属性
    uint32_t local_header_offset;  // 对应局部文件头的偏移量
} ZACentralDirectoryHeader;

// 结束记录结构体
typedef struct {
    uint32_t signature;              // 结束记录签名（0x06054b50）
    uint16_t disk_number;            // 当前磁盘号
    uint16_t central_dir_disk;       // 中央目录起始磁盘号
    uint16_t num_entries_this_disk;  // 本磁盘上的中央目录记录数
    uint16_t total_entries;          // 中央目录中的总记录数
    uint32_t central_dir_size;       // 中央目录大小
    uint32_t central_dir_offset;     // 中央目录偏移量
    uint16_t comment_length;         // ZIP文件注释长度（ALL 2 0）
} ZAEndOfCentralDirectoryRecord;

#pragma pack(pop)

// 用于存储中央目录记录和对应文件名的结构
typedef struct {
    ZACentralDirectoryHeader header;
    char                    *filename;  // 存储在中央目录中的文件名
} ZACDRecord;

// ZIP存档结构体
typedef struct {
    FILE       *fp;          // ZIP文件指针
    ZACDRecord *cd_records;  // 动态数组，保存中央目录记录及文件名
    size_t      cd_count;    // 记录数
    size_t      cd_alloc;    // 分配的记录容量
} ZAInstance;

// 全局CRC32查找表
uint32_t crc32_table[256];

// 初始化CRC32查找表
ZA_INLINE static void init_crc32_table() {
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            if (crc & 1)
                crc = (crc >> 1) ^ polynomial;
            else
                crc >>= 1;
        crc32_table[i] = crc;
    }
}

// 利用查找表计算CRC32
ZA_INLINE static uint32_t get_crc32(FILE *f) {
    uint32_t crc = 0xFFFFFFFF;
    int      ch;
    while ((ch = fgetc(f)) != EOF) crc = (crc >> 8) ^ crc32_table[(crc ^ (uint8_t)ch) & 0xFF];
    return ~crc;
}

// 将time_t转换为DOS格式的日期和时间
ZA_INLINE static void time2dos(time_t t, uint16_t *dos_date, uint16_t *dos_time) {
    struct tm *tm_info = localtime(&t);
    *dos_date          = ((tm_info->tm_year - 80) << 9) | ((tm_info->tm_mon + 1) << 5) | tm_info->tm_mday;
    *dos_time          = (tm_info->tm_hour << 11) | (tm_info->tm_min << 5) | (tm_info->tm_sec >> 1);
}

/// @brief   初始化ZIP存档
/// @param path ZIP文件路径
/// @return  实例指针
ZA_INLINE ZAInstance *za_init(const char *path) {
    init_crc32_table();  // 初始化CRC32查找表
    ZAInstance *za = (ZAInstance *)malloc(sizeof(ZAInstance));
    if (!za) return NULL;  // 内存分配失败
    za->fp = fopen(path, "wb");
    if (!za->fp) {
        free(za);
        return NULL;  // 文件创建失败
    }
    za->cd_records = NULL;
    za->cd_count   = 0;
    za->cd_alloc   = 0;
    return za;
}

/// @brief                   添加文件到ZIP存档
/// @param za                实例指针
/// @param src_path          源文件路径（实际文件）
/// @param dst_path          存储在ZIP中的路径（文件名）
/// @param use_cust_time     是否使用provided_time（非0表示使用）
/// @param cust_time         如果使用，则为指定的修改时间；否则从文件属性获取
/// @return                  非0表示失败
ZA_INLINE int za_add_file(ZAInstance *za, const char *src_path, const char *dst_path, int use_cust_time,
                          time_t cust_time) {
    if (!za || !za->fp) return -1;  // 无指针

    FILE *src = fopen(src_path, "rb");
    if (!src) return -1;  // 无法打开源文件

    // 获取源文件大小
    fseek(src, 0, SEEK_END);
    uint32_t file_size = ftell(src);
    fseek(src, 0, SEEK_SET);

    // 获取文件修改时间
    time_t mod_time;
    if (use_cust_time)
        mod_time = cust_time;
    else {
        struct stat st;
        if (stat(src_path, &st) == 0)
            mod_time = st.st_mtime;
        else
            mod_time = time(NULL);
    }
    uint16_t dos_date, dos_time;
    time2dos(mod_time, &dos_date, &dos_time);

    // 计算CRC32
    uint32_t crc32 = get_crc32(src);
    fseek(src, 0, SEEK_SET);

    // 记录局部文件头偏移
    uint32_t local_header_offset = ftell(za->fp);

    // 写入局部文件头
    ZALocalFileHeader local_header  = {0};
    local_header.signature          = 0x04034b50;
    local_header.version_needed     = 20;
    local_header.general_purpose    = 0;
    local_header.compression_method = 0;  // 存档，不压缩
    local_header.last_mod_time      = dos_time;
    local_header.last_mod_date      = dos_date;
    local_header.crc32              = crc32;
    local_header.compressed_size    = file_size;
    local_header.uncompressed_size  = file_size;
    local_header.filename_length    = strlen(dst_path);
    local_header.extra_field_length = 0;

    fwrite(&local_header, sizeof(ZALocalFileHeader), 1, za->fp);
    // 写入存储在ZIP中的文件名（dst_path）
    fwrite(dst_path, strlen(dst_path), 1, za->fp);

    // 写入文件数据
    char   buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) fwrite(buffer, 1, bytes_read, za->fp);
    fclose(src);

    // 构造中央目录记录
    ZACentralDirectoryHeader cd_header = {0};
    cd_header.signature                = 0x02014b50;
    cd_header.version_made_by          = 20;
    cd_header.version_needed           = 20;
    cd_header.general_purpose          = 0;
    cd_header.compression_method       = 0;
    cd_header.last_mod_time            = dos_time;
    cd_header.last_mod_date            = dos_date;
    cd_header.crc32                    = crc32;
    cd_header.compressed_size          = file_size;
    cd_header.uncompressed_size        = file_size;
    cd_header.filename_length          = strlen(dst_path);
    cd_header.extra_field_length       = 0;
    cd_header.file_comment_length      = 0;
    cd_header.disk_number_start        = 0;
    cd_header.internal_attributes      = 0;
    cd_header.external_attributes      = 0;
    cd_header.local_header_offset      = local_header_offset;

    // 扩充中央目录记录数组空间
    if (za->cd_count == za->cd_alloc) {
        size_t new_alloc = (za->cd_alloc == 0) ? 4 : za->cd_alloc * 2;
        void  *new_ptr   = realloc(za->cd_records, new_alloc * sizeof(ZACDRecord));
        if (!new_ptr) return -1;  // 内存扩展失败
        za->cd_records = (ZACDRecord *)new_ptr;
        za->cd_alloc   = new_alloc;
    }
    // 保存中央目录记录和对应文件名（复制dst_path字符串）
    za->cd_records[za->cd_count].header   = cd_header;
    za->cd_records[za->cd_count].filename = strdup(dst_path);
    if (!za->cd_records[za->cd_count].filename) return -1;  // 文件名复制失败
    za->cd_count++;

    return 0;
}

/// @brief    完成ZIP文件写入，写入中央目录和结束记录，然后关闭文件
/// @param za 示例指针
/// @return   非0表示失败
ZA_INLINE int za_ok(ZAInstance *za) {
    if (!za || !za->fp) return -1;

    // 记录中央目录开始偏移
    uint32_t central_dir_offset = ftell(za->fp);

    // 写入每个中央目录记录
    for (size_t i = 0; i < za->cd_count; i++) {
        // 写入中央目录头
        fwrite(&za->cd_records[i].header, sizeof(ZACentralDirectoryHeader), 1, za->fp);
        // 写入存储在中央目录中的文件名
        fwrite(za->cd_records[i].filename, strlen(za->cd_records[i].filename), 1, za->fp);
    }

    // 计算中央目录大小
    uint32_t central_dir_size = ftell(za->fp) - central_dir_offset;

    // 写入结束记录
    ZAEndOfCentralDirectoryRecord end_record = {0};
    end_record.signature                     = 0x06054b50;
    end_record.disk_number                   = 0;
    end_record.central_dir_disk              = 0;
    end_record.num_entries_this_disk         = za->cd_count;
    end_record.total_entries                 = za->cd_count;
    end_record.central_dir_size              = central_dir_size;
    end_record.central_dir_offset            = central_dir_offset;
    end_record.comment_length                = 0;
    fwrite(&end_record, sizeof(ZAEndOfCentralDirectoryRecord), 1, za->fp);

    fclose(za->fp);

    // 清理中央目录记录内存
    for (size_t i = 0; i < za->cd_count; i++) free(za->cd_records[i].filename);
    free(za->cd_records);
    free(za);
    return 0;
}

#ifdef __cplusplus
}
#endif