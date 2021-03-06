////////////////////////////////////////////////////////////////////////
//                                                                    //
//            Trabalho II: Sistema de Gest�o de Ficheiros             //
//                                                                    //
// Compila��o: gcc vfs.c -Wall -lreadline -o vfs                      //
// Utiliza��o: ./vfs [-b[128|256|512|1024]] [-f[7|8|9|10]] FILESYSTEM //
//                                                                    //
////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAXARGS 100
#define CHECK_NUMBER 9999
#define TYPE_DIR 'D'
#define TYPE_FILE 'F'
#define MAX_NAME_LENGHT 20

#define FAT_ENTRIES(TYPE) ((TYPE) == 7 ? 128 : (TYPE) == 8 ? 256 : (TYPE) == 9 ? 512 : 1024)
#define FAT_SIZE(TYPE) (FAT_ENTRIES(TYPE) * sizeof(int))
#define BLOCK(N) (blocks + (N) * sb->block_size)
#define DIR_ENTRIES_PER_BLOCK (sb->block_size / sizeof(dir_entry))

typedef struct command {
  char *cmd;              // string apenas com o comando
  int argc;               // n�mero de argumentos
  char *argv[MAXARGS+1];  // vector de argumentos do comando
} COMMAND;

typedef struct superblock_entry {
  int check_number;   // n�mero que permite identificar o sistema como v�lido
  int block_size;     // tamanho de um bloco {128, 256 (default), 512 ou 1024 bytes}
  int fat_type;       // tipo de FAT {7, 8 (default), 9 ou 10}
  int root_block;     // n�mero do 1� bloco a que corresponde o diret�rio raiz
  int free_block;     // n�mero do 1� bloco da lista de blocos n�o utilizados
  int n_free_blocks;  // total de blocos n�o utilizados
} superblock;

typedef struct directory_entry {
  char type;                   // tipo da entrada (TYPE_DIR ou TYPE_FILE)
  char name[MAX_NAME_LENGHT];  // nome da entrada
  unsigned char day;           // dia em que foi criada (entre 1 e 31)
  unsigned char month;         // mes em que foi criada (entre 1 e 12)
  unsigned char year;          // ano em que foi criada (entre 0 e 255 - 0 representa o ano de 1900)
  int size;                    // tamanho em bytes (0 se TYPE_DIR)
  int first_block;             // primeiro bloco de dados
} dir_entry;

// vari�veis globais
superblock *sb;   // superblock do sistema de ficheiros
int *fat;         // apontador para a FAT
char *blocks;     // apontador para a regi�o dos dados
int current_dir;  // bloco do diret�rio corrente

// fun��es auxiliares
COMMAND parse(char *);
void parse_argv(int, char **);
void show_usage_and_exit(void);
void init_filesystem(int, int, char *);
void init_superblock(int, int);
void init_fat(void);
void init_dir_block(int, int);
void init_dir_entry(dir_entry *, char, char *, int, int);
void exec_com(COMMAND);

// fun��es de manipula��o de diret�rios
void vfs_ls(void);
void vfs_mkdir(char *);
void vfs_cd(char *);
void vfs_pwd(void);
void vfs_rmdir(char *);

// fun��es de manipula��o de ficheiros
void vfs_get(char *, char *);
void vfs_put(char *, char *);
void vfs_cat(char *);
void vfs_cp(char *, char *);
void vfs_mv(char *, char *);
void vfs_rm(char *);


int main(int argc, char *argv[]) {
  char *linha;
  COMMAND com;

  parse_argv(argc, argv);
  while (1) {
    if ((linha = readline("vfs$ ")) == NULL)
      exit(0);
    if (strlen(linha) != 0) {
      add_history(linha);
      com = parse(linha);
      exec_com(com);
    }
    free(linha);
  }
  return 0;
}


COMMAND parse(char *linha) {
  int i = 0;
  COMMAND com;

  com.cmd = strtok(linha, " ");
  com.argv[0] = com.cmd;
  while ((com.argv[++i] = strtok(NULL, " ")) != NULL);
  com.argc = i;
  return com;
}


void parse_argv(int argc, char *argv[]) {
  int i, block_size, fat_type;

  // valores por omiss�o
  block_size = 256;
  fat_type = 8;
  if (argc < 2 || argc > 4) {
    printf("vfs: invalid number of arguments\n");
    show_usage_and_exit();
  }
  for (i = 1; i < argc - 1; i++) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'b') {
	block_size = atoi(&argv[i][2]);
	if (block_size != 128 && block_size != 256 && block_size != 512 && block_size != 1024) {
	  printf("vfs: invalid block size (%d)\n", block_size);
	  show_usage_and_exit();
	}
      } else if (argv[i][1] == 'f') {
	fat_type = atoi(&argv[i][2]);
	if (fat_type != 7 && fat_type != 8 && fat_type != 9 && fat_type != 10) {
	  printf("vfs: invalid fat type (%d)\n", fat_type);
	  show_usage_and_exit();
	}
      } else {
	printf("vfs: invalid argument (%s)\n", argv[i]);
	show_usage_and_exit();
      }
    } else {
      printf("vfs: invalid argument (%s)\n", argv[i]);
      show_usage_and_exit();
    }
  }
  init_filesystem(block_size, fat_type, argv[argc-1]);
  return;
}


void show_usage_and_exit(void) {
  printf("Usage: vfs [-b[128|256|512|1024]] [-f[7|8|9|10]] FILESYSTEM\n");
  exit(1);
}


void init_filesystem(int block_size, int fat_type, char *filesystem_name) {
  int fsd, filesystem_size;

  if ((fsd = open(filesystem_name, O_RDWR)) == -1) {
    // o sistema de ficheiros n�o existe --> � necess�rio cri�-lo e format�-lo
    if ((fsd = open(filesystem_name, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU)) == -1) {
      printf("vfs: cannot create filesystem (%s)\n", filesystem_name);
      show_usage_and_exit();
    }

    // calcula o tamanho do sistema de ficheiros
    filesystem_size = block_size + FAT_SIZE(fat_type) + FAT_ENTRIES(fat_type) * block_size;
    printf("vfs: formatting virtual file-system (%d bytes) ... please wait\n", filesystem_size);

    // estende o sistema de ficheiros para o tamanho desejado
    lseek(fsd, filesystem_size - 1, SEEK_SET);
    write(fsd, "", 1);

    // faz o mapeamento do sistema de ficheiros e inicia as vari�veis globais
    if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      close(fsd);
      printf("vfs: cannot map filesystem (mmap error)\n");
      exit(1);
    }
    fat = (int *) ((unsigned long int) sb + block_size);
    blocks = (char *) ((unsigned long int) fat + FAT_SIZE(fat_type));

    // inicia o superblock
    init_superblock(block_size, fat_type);

    // inicia a FAT
    init_fat();

    // inicia o bloco do diret�rio raiz '/'
    init_dir_block(sb->root_block, sb->root_block);
  } else {
    // calcula o tamanho do sistema de ficheiros
    struct stat buf;
    stat(filesystem_name, &buf);
    filesystem_size = buf.st_size;

    // faz o mapeamento do sistema de ficheiros e inicia as vari�veis globais
    if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      close(fsd);
      printf("vfs: cannot map filesystem (mmap error)\n");
      exit(1);
    }
    fat = (int *) ((unsigned long int) sb + sb->block_size);
    blocks = (char *) ((unsigned long int) fat + FAT_SIZE(sb->fat_type));

    // testa se o sistema de ficheiros � v�lido
    if (sb->check_number != CHECK_NUMBER || filesystem_size != sb->block_size + FAT_SIZE(sb->fat_type) + FAT_ENTRIES(sb->fat_type) * sb->block_size) {
      munmap(sb, filesystem_size);
      close(fsd);
      printf("vfs: invalid filesystem (%s)\n", filesystem_name);
      show_usage_and_exit();
    }
  }
  close(fsd);

  // inicia o diret�rio corrente
  current_dir = sb->root_block;
  return;
}


void init_superblock(int block_size, int fat_type) {
  sb->check_number = CHECK_NUMBER;
  sb->block_size = block_size;
  sb->fat_type = fat_type;
  sb->root_block = 0;
  sb->free_block = 1;
  sb->n_free_blocks = FAT_ENTRIES(fat_type) - 1;
  return;
}


void init_fat(void) {
  int i;

  fat[0] = -1;
  for (i = 1; i < sb->n_free_blocks; i++)
    fat[i] = i + 1;
  fat[sb->n_free_blocks] = -1;
  return;
}


void init_dir_block(int block, int parent_block) {
  dir_entry *dir = (dir_entry *) BLOCK(block);
  // o n�mero de entradas no diret�rio (inicialmente 2) fica guardado no campo size da entrada "."
  init_dir_entry(&dir[0], TYPE_DIR, ".", 2, block);
  init_dir_entry(&dir[1], TYPE_DIR, "..", 0, parent_block);
  return;
}


void init_dir_entry(dir_entry *dir, char type, char *name, int size, int first_block) {
  time_t cur_time = time(NULL);
  struct tm *cur_tm = localtime(&cur_time);

  dir->type = type;
  strcpy(dir->name, name);
  dir->day = cur_tm->tm_mday;
  dir->month = cur_tm->tm_mon + 1;
  dir->year = cur_tm->tm_year;
  dir->size = size;
  dir->first_block = first_block;
  return;
}


void exec_com(COMMAND com) {
  // para cada comando invocar a fun��o que o implementa
  if (!strcmp(com.cmd, "exit")) {
    exit(0);
  } else if (!strcmp(com.cmd, "ls")) {
    if (com.argc > 1)
      printf("ERROR(input: 'ls' - too many arguments)\n");
    else
      vfs_ls();
  } else if (!strcmp(com.cmd, "mkdir")) {
    if (com.argc < 2)
      printf("ERROR(input: 'mkdir' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'mkdir' - too many arguments)\n");
    else
      vfs_mkdir(com.argv[1]);
  } else if (!strcmp(com.cmd, "cd")) {
    if (com.argc < 2)
      printf("ERROR(input: 'cd' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'cd' - too many arguments)\n");
    else
      vfs_cd(com.argv[1]);
  } else if (!strcmp(com.cmd, "pwd")) {
    if (com.argc != 1)
      printf("ERROR(input: 'pwd' - too many arguments)\n");
    else
      vfs_pwd();
  } else if (!strcmp(com.cmd, "rmdir")) {
    if (com.argc < 2)
      printf("ERROR(input: 'rmdir' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'rmdir' - too many arguments)\n");
    else
      vfs_rmdir(com.argv[1]);
  } else if (!strcmp(com.cmd, "get")) {
    if (com.argc < 3)
      printf("ERROR(input: 'get' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'get' - too many arguments)\n");
    else
      vfs_get(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "put")) {
    if (com.argc < 3)
      printf("ERROR(input: 'put' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'put' - too many arguments)\n");
    else
      vfs_put(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "cat")) {
    if (com.argc < 2)
      printf("ERROR(input: 'cat' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'cat' - too many arguments)\n");
    else
      vfs_cat(com.argv[1]);
  } else if (!strcmp(com.cmd, "cp")) {
    if (com.argc < 3)
      printf("ERROR(input: 'cp' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'cp' - too many arguments)\n");
    else
      vfs_cp(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "mv")) {
    if (com.argc < 3)
      printf("ERROR(input: 'mv' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'mv' - too many arguments)\n");
    else
      vfs_mv(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "rm")) {
    if (com.argc < 2)
      printf("ERROR(input: 'rm' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'rm' - too many arguments)\n");
    else
      vfs_rm(com.argv[1]);
  } else
    printf("ERROR(input: command not found)\n");
  return;
}

int get_free_block() {
  int block;
  if ((block = sb->free_block) != -1) {
    sb->free_block = fat[block];
    fat[block] = -1;
    sb->n_free_blocks--;
  }
  return block;
}

void free_block(int block) {
  if (fat[block] != -1) {
    free_block(fat[block]);
  }
  fat[block] = sb->free_block;
  sb->free_block = block;
  sb->n_free_blocks++;
}

int is_dir_empty(int block) {
  dir_entry *dir = (dir_entry*) BLOCK(block);
  return (dir[0].size == 2);
}

int is_dir_full(int block) {
  dir_entry *dir = (dir_entry*) BLOCK(block);
  if (dir[0].size == DIR_ENTRIES_PER_BLOCK) {
    return 1;
  }
  return 0;
}

dir_entry* find_dir_entry(int block, char *name) {
  dir_entry *dir = (dir_entry*) BLOCK(block);
  int i, n_entries = dir[0].size;
  for(i=0; i < n_entries; i++) {
    if (i > 0 && i % DIR_ENTRIES_PER_BLOCK == 0) {
      block = fat[block];
      dir = (dir_entry*) BLOCK(block);
    }
    if (strcmp(dir[i%DIR_ENTRIES_PER_BLOCK].name, name) == 0) {
      return &dir[i%DIR_ENTRIES_PER_BLOCK];
    }
  }
  return NULL;
}

void add_dir_entry(int block, int size, char *name, int first_block, char type) {
  dir_entry *dir = (dir_entry*) BLOCK(block);
  int pos = dir[0].size % DIR_ENTRIES_PER_BLOCK;
  dir[0].size++;
  while (fat[block] != -1) {
    block = fat[block];
  }
  if (pos == 0) {
    fat[block] = get_free_block();
    block = fat[block];
  }
  dir = (dir_entry*) BLOCK(block);
  init_dir_entry(&dir[pos], type, name, size, first_block);
}

void remove_dir_entry(int block, dir_entry *rmdir) {
    dir_entry *dir, *last_entry;
    int last_pos, previous_block;
    dir = (dir_entry*) BLOCK(block);
    last_pos = (dir[0].size - 1)%DIR_ENTRIES_PER_BLOCK;
    dir[0].size--;
    while (fat[block] != -1) {
      previous_block = block;
      block = fat[block];
    }
    dir = (dir_entry*) BLOCK(block);
    last_entry = &dir[last_pos];
    memcpy(rmdir,last_entry,sizeof(dir_entry));
    if (last_pos == 0) {
      free_block(block);
      fat[previous_block] = -1;
    }
}

void get_file (int id, int size, int block) {
  read(id, BLOCK(block), sb->block_size);
  while (size > sb->block_size) {
    size -= sb->block_size;
    block = fat[block];
    read(id, BLOCK(block), sb->block_size);
  }
}

void delete_block(int block) {
  fat[block] = sb->free_block;
  sb->free_block = block;
  sb->n_free_blocks++;
  return;
}

//qsort compare
int qsort_lscmp (const void *s1, const void *s2) {
  return strcmp((**(dir_entry**)s1).name, (**(dir_entry**)s2).name);
}

// ls - lista o conte�do do diret�rio actual
void vfs_ls(void) {
  dir_entry *dir, **qsort_dir;
  char *mes[12] = {"Jan","Fev","Mar","Abr","Mai","Jun","Jul","Ago","Set","Out","Nov","Dez"};
  int i, n_entries, current_block;
  dir = (dir_entry*) BLOCK(current_dir);
  n_entries = dir[0].size;
  qsort_dir = (dir_entry**) malloc(n_entries*sizeof(dir_entry*));
  current_block = current_dir;
  for (i = 0; i < n_entries; i++) {
    if (i && i % DIR_ENTRIES_PER_BLOCK == 0) {
      current_block = fat[current_block];
      dir = (dir_entry*) BLOCK(current_block);
    }
    qsort_dir[i] = &dir[i%DIR_ENTRIES_PER_BLOCK];
  }
  qsort(qsort_dir, n_entries, sizeof(dir_entry*), qsort_lscmp);
  for (i = 0; i < n_entries; i++) {
    if(qsort_dir[i]->type == TYPE_DIR) {
      printf("%s %d-%s-%d DIR\n", qsort_dir[i]->name, qsort_dir[i]->day, mes[(qsort_dir[i]->month)-1], qsort_dir[i]->year + 1900);
    }
    else {
      printf("%d \n",qsort_dir[i]->size);
    }
  }
  return;
}


// mkdir dir - cria um subdiret�rio com nome dir no diret�rio actual
void vfs_mkdir(char *nome_dir) {
  dir_entry *dir = (dir_entry*) BLOCK(current_dir);
  int n_entries = dir[0].size;
  int req_blocks = (n_entries % DIR_ENTRIES_PER_BLOCK == 0) + 1;
  if (sb->n_free_blocks < req_blocks) {
    printf("ERROR(mkdir: memory full)\n");
    return;
  }
  dir[0].size++; //numero de entradas do bloco incrementa
  int new_block = get_free_block();
  init_dir_block(new_block, current_dir);
  int current_block = current_dir;
  while (fat[current_block] != -1) {
    current_block = fat[current_block];
  }
  if (n_entries % DIR_ENTRIES_PER_BLOCK == 0) {
    int next_block = get_free_block();
    fat[current_block] = next_block;
    current_block = next_block;
  }
  dir = (dir_entry*) BLOCK(current_block);
  init_dir_entry(&dir[n_entries % DIR_ENTRIES_PER_BLOCK], TYPE_DIR, nome_dir, 0, new_block);
  return;
}


// cd dir - move o diret�rio actual para dir
void vfs_cd(char *nome_dir) {
  dir_entry *dir = (dir_entry*) BLOCK(current_dir);
  int n_entries = dir[0].size, i;
  int current_block = current_dir;
  for (i=0; i < n_entries; i++) {
    if (i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
        current_block = fat[current_block];
        dir = (dir_entry*) BLOCK(current_block);
      }
    int block_i = i % DIR_ENTRIES_PER_BLOCK;
    if (dir[block_i].type == TYPE_DIR && strcmp(dir[block_i].name, nome_dir) == 0) {
      current_dir = dir[block_i].first_block;
      return;
    }
  }
  printf("ERROR(cd: directory not found)\n");
  return;
}


// pwd - escreve o caminho absoluto do diret�rio actual
void vfs_pwd(void) {
  return;
}


// rmdir dir - remove o subdiret�rio dir (se vazio) do diret�rio actual 1º
void vfs_rmdir(char *nome_dir) {
  if (strcmp(".", nome_dir) == 0 || strcmp("..", nome_dir) == 0) {
    printf("ERROR(rmdir: can't remove this directory)\n");
    return;
  }
  else {
    dir_entry *dir = find_dir_entry(current_dir,nome_dir);
    if (dir == NULL) {
      printf("ERROR(rmdir: directory not found)\n");
      return;
    }
    if (dir->type != TYPE_DIR) {
      printf("ERROR(rmdir: not a directory)\n");
      return;
    }
    else if (!is_dir_empty(dir->first_block)) {
      printf("ERROR(rmdir: directory not empty)\n");
      return;
    }
    else {
      free_block(dir->first_block);
      remove_dir_entry(current_dir,dir);
    }
  }
  return;
}


// get fich1 fich2 - copia um ficheiro normal UNIX fich1 para um ficheiro no nosso sistema fich2 4º
void vfs_get(char *nome_orig, char *nome_dest) {
  int fd;
  if ((fd = open(nome_orig,O_RDONLY)) == -1) {
    printf("ERROR(get: file not found)\n");
    return;
  }
  else {
    struct stat fstat;
    if(lstat(nome_orig,&fstat) == -1) {
      //error
      return;
    }
    else if(strlen(nome_orig) > MAX_NAME_LENGHT) {
      //ERROR
      return;
    }
    else if(find_dir_entry(current_dir, nome_dest) != NULL) {
      //ERROR
      return;
    }
    else if(sb->n_free_blocks < (sb->block_size-1 + fstat.st_size) / sb->block_size + is_dir_full(current_dir)) {
      //error
      return;
    }
    else {
      int first_block = get_free_block();
      get_file(fd, fstat.st_size, first_block);
      add_dir_entry(current_dir, fstat.st_size, nome_dest, first_block, TYPE_FILE);
    }
    close(fd);
  }
  return;
}


// put fich1 fich2 - copia um ficheiro do nosso sistema fich1 para um ficheiro normal UNIX fich2 2º
void vfs_put(char *nome_orig, char *nome_dest) {
  return;
}


// cat fich - escreve para o ecr� o conte�do do ficheiro fich
void vfs_cat(char *nome_fich) {
  return;
}


// cp fich1 fich2 - copia o ficheiro fich1 para fich2
// cp fich dir - copia o ficheiro fich para o subdiret�rio dir
void vfs_cp(char *nome_orig, char *nome_dest) {
  return;
}


// mv fich1 fich2 - move o ficheiro fich1 para fich2
// mv fich dir - move o ficheiro fich para o subdiret�rio dir
void vfs_mv(char *nome_orig, char *nome_dest) {
  return;
}


// rm fich - remove o ficheiro fich 3º
void vfs_rm(char *nome_fich) {
  return;
}
