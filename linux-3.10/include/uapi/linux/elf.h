#ifndef _UAPI_LINUX_ELF_H
#define _UAPI_LINUX_ELF_H

#include <linux/types.h>
#include <linux/elf-em.h>

/* 32-bit ELF base types. */
typedef __u32 Elf32_Addr;
typedef __u16 Elf32_Half;
typedef __u32 Elf32_Off;
typedef __s32 Elf32_Sword;
typedef __u32 Elf32_Word;

/* 64-bit ELF base types. */
typedef __u64 Elf64_Addr;
typedef __u16 Elf64_Half;
typedef __s16 Elf64_SHalf;
typedef __u64 Elf64_Off;
typedef __s32 Elf64_Sword;
typedef __u32 Elf64_Word;
typedef __u64 Elf64_Xword;
typedef __s64 Elf64_Sxword;

/* These constants are for the segment types stored in the image headers */ // 程序头属性
#define PT_NULL 0
#define PT_LOAD 1 // 需要装载映射
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6
#define PT_TLS 7           /* Thread local storage segment */
#define PT_LOOS 0x60000000 /* OS-specific */
#define PT_HIOS 0x6fffffff /* OS-specific */
#define PT_LOPROC 0x70000000
#define PT_HIPROC 0x7fffffff
#define PT_GNU_EH_FRAME 0x6474e550

#define PT_GNU_STACK (PT_LOOS + 0x474e551)

/*
 * Extended Numbering
 *
 * If the real number of program header table entries is larger than
 * or equal to PN_XNUM(0xffff), it is set to sh_info field of the
 * section header at index 0, and PN_XNUM is set to e_phnum
 * field. Otherwise, the section header at index 0 is zero
 * initialized, if it exists.
 *
 * Specifications are available in:
 *
 * - Oracle: Linker and Libraries.
 *   Part No: 817–1984–19, August 2011.
 *   http://docs.oracle.com/cd/E18752_01/pdf/817-1984.pdf
 *
 * - System V ABI AMD64 Architecture Processor Supplement
 *   Draft Version 0.99.4,
 *   January 13, 2010.
 *   http://www.cs.washington.edu/education/courses/cse351/12wi/supp-docs/abi.pdf
 */
#define PN_XNUM 0xffff

/* These constants define the different elf file types */ // elf文件类型
#define ET_NONE 0
#define ET_REL 1  // 可重定位文件
#define ET_EXEC 2 // 可执行文件
#define ET_DYN 3  // 动态库文件
#define ET_CORE 4 // 核心转储文件
#define ET_LOPROC 0xff00
#define ET_HIPROC 0xffff

/* This is the info that is needed to parse the dynamic section of the file */ //.dynamic动态段节点类型
#define DT_NULL 0
#define DT_NEEDED 1 // 依赖的共享对象文件 d_ptr表示文件名
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4    // 动态链接哈希表地址
#define DT_STRTAB 5  // 动态连接字符串表地址
#define DT_SYMTAB 6  // 动态链接表地址
#define DT_RELA 7    // 动态链接重定位表地址
#define DT_RELASZ 8  // 重定位表的大小
#define DT_RELAENT 9 // 重定位项的大小
#define DT_STRSZ 10  // 动态链接字符串表大小
#define DT_SYMENT 11
#define DT_INIT 12   // 初始化代码地址
#define DT_FINI 13   // 结束代码地址
#define DT_SONAME 14 // 共享对象的soname
#define DT_RPATH 15  // 动态链接共享对象搜索路径
#define DT_SYMBOLIC 16
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_DEBUG 21
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_ENCODING 32
#define OLD_DT_LOOS 0x60000000
#define DT_LOOS 0x6000000d
#define DT_HIOS 0x6ffff000
#define DT_VALRNGLO 0x6ffffd00
#define DT_VALRNGHI 0x6ffffdff
#define DT_ADDRRNGLO 0x6ffffe00
#define DT_ADDRRNGHI 0x6ffffeff
#define DT_VERSYM 0x6ffffff0
#define DT_RELACOUNT 0x6ffffff9
#define DT_RELCOUNT 0x6ffffffa
#define DT_FLAGS_1 0x6ffffffb
#define DT_VERDEF 0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd
#define DT_VERNEED 0x6ffffffe
#define DT_VERNEEDNUM 0x6fffffff
#define OLD_DT_HIOS 0x6fffffff
#define DT_LOPROC 0x70000000
#define DT_HIPROC 0x7fffffff

/* This info is needed when parsing the symbol table */ // 符号绑定信息
#define STB_LOCAL 0                                     // 局部符号
#define STB_GLOBAL 1                                    // 全局符号
#define STB_WEAK 2                                      // 弱符号
// 符号类型
#define STT_NOTYPE 0  // 未知
#define STT_OBJECT 1  // 数据对象
#define STT_FUNC 2    // 函数
#define STT_SECTION 3 // 段
#define STT_FILE 4    // 文件名
#define STT_COMMON 5  // 未初始化的通用块
#define STT_TLS 6     // 线程局部对象

#define ELF_ST_BIND(x) ((x) >> 4)                // 提取绑定信息
#define ELF_ST_TYPE(x) (((unsigned int)x) & 0xf) // 提取类型
#define ELF32_ST_BIND(x) ELF_ST_BIND(x)
#define ELF32_ST_TYPE(x) ELF_ST_TYPE(x)
#define ELF64_ST_BIND(x) ELF_ST_BIND(x)
#define ELF64_ST_TYPE(x) ELF_ST_TYPE(x)

typedef struct dynamic // .dynamic段节点结构
{
  Elf32_Sword d_tag; // 类型标识 DT_XXX
  union
  {
    Elf32_Sword d_val; // 值
    Elf32_Addr d_ptr;  // 地址
  } d_un;
} Elf32_Dyn;

typedef struct
{
  Elf64_Sxword d_tag; /* entry tag value */
  union
  {
    Elf64_Xword d_val;
    Elf64_Addr d_ptr;
  } d_un;
} Elf64_Dyn;

/* The following are used with relocations */ // 提取符号重定位信息
#define ELF32_R_SYM(x) ((x) >> 8)             // 提取符号重定位绑定信息
#define ELF32_R_TYPE(x) ((x)&0xff)            // 提取符号重定位类型

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i)&0xffffffff)

typedef struct elf32_rel // 重定位表入口结构
{
  Elf32_Addr r_offset; // 段偏移或虚拟地址
  Elf32_Word r_info;   // 低8位标识入口类型 高24位标识入口符号在符号表的下标
} Elf32_Rel;

typedef struct elf64_rel
{
  Elf64_Addr r_offset; /* Location at which to apply the action */
  Elf64_Xword r_info;  /* index and type of relocation */
} Elf64_Rel;

typedef struct elf32_rela // 重定位表入口结构
{
  Elf32_Addr r_offset;  // 段偏移或虚拟地址
  Elf32_Word r_info;    // 低8位标识入口类型 高24位标识入口符号在符号表的下标
  Elf32_Sword r_addend; // 辅助计算修订值 某些指令使用的是下一条指令的地址作为偏移寻址，则可以将这部分的偏移信息放在r_addend里面
} Elf32_Rela;

typedef struct elf64_rela
{
  Elf64_Addr r_offset;   /* Location at which to apply the action */
  Elf64_Xword r_info;    /* index and type of relocation */
  Elf64_Sxword r_addend; /* Constant addend used to compute value */
} Elf64_Rela;

typedef struct elf32_sym // 符号表结构
{
  Elf32_Word st_name;     // 字符串表中的索引
  Elf32_Addr st_value;    // 符号值 绝对值或在段中偏移的地址值
  Elf32_Word st_size;     // 符号大小
  unsigned char st_info;  // 低4位标识符号类型 高4位标识绑定信息
  unsigned char st_other; // 0
  Elf32_Half st_shndx;    // 符号所在的段
} Elf32_Sym;

typedef struct elf64_sym
{
  Elf64_Word st_name;     /* Symbol name, index in string tbl */
  unsigned char st_info;  /* Type and binding attributes */
  unsigned char st_other; /* No defined meaning, 0 */
  Elf64_Half st_shndx;    /* Associated section index */
  Elf64_Addr st_value;    /* Value of the symbol */
  Elf64_Xword st_size;    /* Associated symbol size */
} Elf64_Sym;

#define EI_NIDENT 16

typedef struct elf32_hdr // elf文件头
{
  unsigned char e_ident[EI_NIDENT];     // elf文件标识
  Elf32_Half e_type;                    // elf文件类型
  Elf32_Half e_machine;                 // elf文件机器架构
  Elf32_Word e_version;                 // elf文件版本号
  Elf32_Addr e_entry; /* Entry point */ // elf执行入口点
  Elf32_Off e_phoff;                    // program header table的偏移
  Elf32_Off e_shoff;                    // section header table的偏移
  Elf32_Word e_flags;                   // 特定于处理器的标志
  Elf32_Half e_ehsize;                  // ELF文件头的大小，32位ELF是52字节，64位是64字节
  Elf32_Half e_phentsize;               // program header table中每个入口的大小
  Elf32_Half e_phnum;                   // program header table的入口个数
  Elf32_Half e_shentsize;               // section header table中每个入口的大小
  Elf32_Half e_shnum;                   // section header table的入口个数
  Elf32_Half e_shstrndx;                // section header table中字符串段(.shstrtab)的索引
} Elf32_Ehdr;

typedef struct elf64_hdr
{
  unsigned char e_ident[EI_NIDENT]; /* ELF "magic number" */
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry; /* Entry point virtual address */
  Elf64_Off e_phoff;  /* Program header table file offset */
  Elf64_Off e_shoff;  /* Section header table file offset */
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
} Elf64_Ehdr;
// 程序头权限属性
/* These constants define the permissions on sections in the program
   header, p_flags. */
#define PF_R 0x4 // 可读
#define PF_W 0x2 // 可写
#define PF_X 0x1 // 可运行

typedef struct elf32_phdr // elf程序头表 segment
{
  Elf32_Word p_type;   // 段类型 PT_XXX
  Elf32_Off p_offset;  // 段位于文件的起始位置
  Elf32_Addr p_vaddr;  // 虚拟地址空间
  Elf32_Addr p_paddr;  // 物理装载地址
  Elf32_Word p_filesz; // 段文件长度
  Elf32_Word p_memsz;  // 段虚拟空间长度
  Elf32_Word p_flags;  // 权限属性
  Elf32_Word p_align;  // 对齐幂数
} Elf32_Phdr;

typedef struct elf64_phdr
{
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;   /* Segment file offset */
  Elf64_Addr p_vaddr;   /* Segment virtual address */
  Elf64_Addr p_paddr;   /* Segment physical address */
  Elf64_Xword p_filesz; /* Segment size in file */
  Elf64_Xword p_memsz;  /* Segment size in memory */
  Elf64_Xword p_align;  /* Segment alignment, file & memory */
} Elf64_Phdr;

/* sh_type */          // 段类型
#define SHT_NULL 0     // 无效段
#define SHT_PROGBITS 1 // 程序数据段
#define SHT_SYMTAB 2   // 符号表
#define SHT_STRTAB 3   // 字符串表
#define SHT_RELA 4     // 重定位表
#define SHT_HASH 5     // 符号表的哈希表
#define SHT_DYNAMIC 6  // 动态链接信息
#define SHT_NOTE 7     // 提示信息
#define SHT_NOBITS 8   // 没有内容
#define SHT_REL 9      // 包含重定位信息
#define SHT_SHLIB 10   // 保留
#define SHT_DYNSYM 11  // 动态链接的符号表
#define SHT_NUM 12
#define SHT_LOPROC 0x70000000
#define SHT_HIPROC 0x7fffffff
#define SHT_LOUSER 0x80000000
#define SHT_HIUSER 0xffffffff

/* sh_flags */            // 段标记
#define SHF_WRITE 0x1     // 可写
#define SHF_ALLOC 0x2     // 需要分配空间
#define SHF_EXECINSTR 0x4 // 可执行
#define SHF_MASKPROC 0xf0000000

/* special section indexes */ // 特殊段索引值
#define SHN_UNDEF 0           // 符号未定义 可能在其它模块中定义
#define SHN_LORESERVE 0xff00
#define SHN_LOPROC 0xff00
#define SHN_HIPROC 0xff1f
#define SHN_ABS 0xfff1    // 标识符号值是绝对值
#define SHN_COMMON 0xfff2 // 标识符号是COMMON块的符号
#define SHN_HIRESERVE 0xffff

typedef struct elf32_shdr // elf段表描述结构
{
  Elf32_Word sh_name;      //.shstrtab中的索引
  Elf32_Word sh_type;      // 段类型
  Elf32_Word sh_flags;     // 段标志
  Elf32_Addr sh_addr;      // 段虚拟地址
  Elf32_Off sh_offset;     // 段在文件中的偏移
  Elf32_Word sh_size;      // 段大小
  Elf32_Word sh_link;      // 段使用的字符串表或重定位段使用的符号表在段表中的索引
  Elf32_Word sh_info;      // 重定位表所作用的段在段表中的索引 或 最后一个局部符号的索引
  Elf32_Word sh_addralign; // 段对齐 2的n次幂
  Elf32_Word sh_entsize;   // 段中每项大小(如果可用)
} Elf32_Shdr;

typedef struct elf64_shdr
{
  Elf64_Word sh_name;       /* Section name, index in string tbl */
  Elf64_Word sh_type;       /* Type of section */
  Elf64_Xword sh_flags;     /* Miscellaneous section attributes */
  Elf64_Addr sh_addr;       /* Section virtual addr at execution */
  Elf64_Off sh_offset;      /* Section file offset */
  Elf64_Xword sh_size;      /* Size of section in bytes */
  Elf64_Word sh_link;       /* Index of another section */
  Elf64_Word sh_info;       /* Additional section information */
  Elf64_Xword sh_addralign; /* Section alignment */
  Elf64_Xword sh_entsize;   /* Entry size if section holds table */
} Elf64_Shdr;

#define EI_MAG0 0 /* e_ident[] indexes */
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_PAD 8

#define ELFMAG0 0x7f /* EI_MAG */
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFMAG "\177ELF"
#define SELFMAG 4

#define ELFCLASSNONE 0 /* EI_CLASS */
#define ELFCLASS32 1   // 32位标识
#define ELFCLASS64 2   // 64位标识
#define ELFCLASSNUM 3

#define ELFDATANONE 0 /* e_ident[EI_DATA] */
#define ELFDATA2LSB 1 // 小端对齐标识
#define ELFDATA2MSB 2 // 大端对齐标识
// elf文件版本号
#define EV_NONE 0 /* e_version, EI_VERSION */
#define EV_CURRENT 1
#define EV_NUM 2

#define ELFOSABI_NONE 0
#define ELFOSABI_LINUX 3

#ifndef ELF_OSABI
#define ELF_OSABI ELFOSABI_NONE
#endif

/*
 * Notes used in ET_CORE. Architectures export some of the arch register sets
 * using the corresponding note types via the PTRACE_GETREGSET and
 * PTRACE_SETREGSET requests.
 */
#define NT_PRSTATUS 1
#define NT_PRFPREG 2
#define NT_PRPSINFO 3
#define NT_TASKSTRUCT 4
#define NT_AUXV 6
/*
 * Note to userspace developers: size of NT_SIGINFO note may increase
 * in the future to accomodate more fields, don't assume it is fixed!
 */
#define NT_SIGINFO 0x53494749
#define NT_FILE 0x46494c45
#define NT_PRXFPREG 0x46e62b7f    /* copied from gdb5.1/include/elf/common.h */
#define NT_PPC_VMX 0x100          /* PowerPC Altivec/VMX registers */
#define NT_PPC_SPE 0x101          /* PowerPC SPE/EVR registers */
#define NT_PPC_VSX 0x102          /* PowerPC VSX registers */
#define NT_386_TLS 0x200          /* i386 TLS slots (struct user_desc) */
#define NT_386_IOPERM 0x201       /* x86 io permission bitmap (1=deny) */
#define NT_X86_XSTATE 0x202       /* x86 extended state using xsave */
#define NT_S390_HIGH_GPRS 0x300   /* s390 upper register halves */
#define NT_S390_TIMER 0x301       /* s390 timer register */
#define NT_S390_TODCMP 0x302      /* s390 TOD clock comparator register */
#define NT_S390_TODPREG 0x303     /* s390 TOD programmable register */
#define NT_S390_CTRS 0x304        /* s390 control registers */
#define NT_S390_PREFIX 0x305      /* s390 prefix register */
#define NT_S390_LAST_BREAK 0x306  /* s390 breaking event address */
#define NT_S390_SYSTEM_CALL 0x307 /* s390 system call restart data */
#define NT_S390_TDB 0x308         /* s390 transaction diagnostic block */
#define NT_ARM_VFP 0x400          /* ARM VFP/NEON registers */
#define NT_ARM_TLS 0x401          /* ARM TLS register */
#define NT_ARM_HW_BREAK 0x402     /* ARM hardware breakpoint registers */
#define NT_ARM_HW_WATCH 0x403     /* ARM hardware watchpoint registers */
#define NT_METAG_CBUF 0x500       /* Metag catch buffer registers */
#define NT_METAG_RPIPE 0x501      /* Metag read pipeline state */
#define NT_METAG_TLS 0x502        /* Metag TLS pointer */

/* Note header in a PT_NOTE section */
typedef struct elf32_note
{
  Elf32_Word n_namesz; /* Name size */
  Elf32_Word n_descsz; /* Content size */
  Elf32_Word n_type;   /* Content type */
} Elf32_Nhdr;

/* Note header in a PT_NOTE section */
typedef struct elf64_note
{
  Elf64_Word n_namesz; /* Name size */
  Elf64_Word n_descsz; /* Content size */
  Elf64_Word n_type;   /* Content type */
} Elf64_Nhdr;

#endif /* _UAPI_LINUX_ELF_H */
