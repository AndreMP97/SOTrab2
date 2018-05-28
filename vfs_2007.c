/*********************************************************************
*                   SISTEMAS DE OPERAÇÃO 2007/2008                   *
*                   ------------------------------                   *
*             Trabalho II: Sistema de Ficheiros Virtual              *
*                                                                    *
* Grupo: tsj                                                         *
* Elemento 1: Tiago Gomes                                            *
* Elemento 2: Sergio Gomes                                           *
*                                                                    *
*********************************************************************/

#include "vfs.h"


int main(int argc, char *argv[])
{
  char *line;
  COMMAND com;

  parse_argv(argc, argv);

  while( 1 ) 
  {
    if( (line = readline("vfs$ ")) == NULL )
      exit(0);

    if( strlen(line) != 0 ) {
      add_history(line);
      com = parse(line);
      exec_com(com);
    }
    free(line);
  }

  return 0;
}




/*-----------------------------------------------------------------------------------------------------
 Funções de manipulação de directórios
------------------------------------------------------------------------------------------------------*/


void parse_argv(int argc, char *argv[])
{
  int i, fsd, block_size, fat_type;

  //block_size = 512; // valor por defeito
  fat_type = 10;    // valor por defeito
  block_size = 128;

  for (i = 1; i < argc - 1; i++) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'b') {
        block_size = atoi(&argv[i][2]);
        if (block_size != 256 && block_size != 512 && block_size != 1024) {
          printf("vfs: invalid block size (%d)\n", block_size);
          printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
          exit(1);
        }
      } else if (argv[i][1] == 'f') {
        fat_type = atoi(&argv[i][2]);
        if (fat_type != 8 && fat_type != 10 && fat_type != 12) {
          printf("vfs: invalid fat type (%d)\n", fat_type);
          printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
          exit(1); 
        }
      } else {
        printf("vfs: invalid argument (%s)\n", argv[i]);
        printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
        exit(1);
      }
    } else {
      printf("vfs: invalid argument (%s)\n", argv[i]);
      printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
      exit(1);
    }
  }

  if ((fsd = open(argv[argc-1], O_RDWR)) == -1) {
    // o sistema de ficheiros não existe --> é necessário criá-lo
    if ((fsd = open(argv[argc-1], O_CREAT | O_TRUNC | O_RDWR, S_IRWXU)) == -1) {
      printf("vfs: cannot create filesystem (%s)\n", argv[argc-1]);
      printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
      exit(1);
    }
    // e formatá-lo
    my_format(fsd, block_size, fat_type);
  } else {
    // faz o mapeamento do sistema de ficheiros e inicia as variáveis globais
    int filesystem_size;
    struct stat buf;
    stat(argv[argc-1], &buf);
    if ((sb = (superblock *) mmap(NULL, buf.st_size, PROT_READ | PROT_WRITE, 
                                  MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      printf("vfs: cannot map filesystem (mmap error)\n");
      close(fsd);
      exit(1);
    }
    fat = (int *) ((int) sb + sb->block_size);
    blocks = (char *) ((int) fat + FAT_SIZE(sb->fat_type));
    dir = (dir_entry *)BLOCK( sb->root_block );    

    // testa se o sistema de ficheiros é válido 
    filesystem_size = sb->block_size + FAT_SIZE(sb->fat_type) + FAT_ENTRIES(sb->fat_type) * sb->block_size;
    if (sb->check_number != CHECK_NUMBER || buf.st_size != filesystem_size) {
      printf("vfs: invalid filesystem (%s)\n", argv[argc-1]);
      printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
      munmap(sb, buf.st_size);
      close(fsd);
      exit(1);
    }
  }
  close(fsd);
}




void my_format(int fsd, int block_size, int fat_type)
{
  int i, filesystem_size;
  struct tm *cur_tm;
  time_t cur_time;

  // calcula o tamanho do sistema de ficheiros
  filesystem_size = block_size + FAT_SIZE(fat_type) + FAT_ENTRIES(fat_type) * block_size;
  printf("vfs: formatting virtual file-system (%d bytes) ... please wait\n", filesystem_size);

  // estende o sistema de ficheiros para o tamanho desejado
  lseek(fsd, filesystem_size - 1, SEEK_SET);
  write(fsd, "", 1);

  // faz o mapeamento do sistema de ficheiros e inicia as variáveis globais
  if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, 
                                MAP_SHARED, fsd, 0)) == MAP_FAILED) {
    printf("vfs: cannot map filesystem (mmap error)\n");
    close(fsd);
    exit(1);
  }
  fat = (int *) ((int) sb + block_size);
  blocks = (char *) ((int) fat + FAT_SIZE(fat_type));

  // inicia o superblock
  sb->check_number = CHECK_NUMBER;
  sb->block_size = block_size;
  sb->fat_type = fat_type;
  sb->root_block = 0;
  sb->free_block = 1;

  // inicia a FAT
  fat[0] = -1;
  for (i = 1; i < FAT_ENTRIES(fat_type) - 1; i++)
    fat[i] = i + 1;
  fat[i] = -1;

  // inicia o bloco do directório raíz
  cur_time = time(NULL);
  cur_tm = localtime(&cur_time);
  dir = (dir_entry *) BLOCK(0);
  dir[0].type = dir[1].type = TYPE_DIR;
  strcpy(dir[0].name, ".");
  strcpy(dir[1].name, "..");
  dir[0].day = dir[1].day = cur_tm->tm_mday;
  dir[0].month = dir[1].month = cur_tm->tm_mon + 1;
  dir[0].year = dir[1].year = cur_tm->tm_year;
  dir[0].size = dir[1].size = 0;
  dir[0].first_block = dir[1].first_block = 0;
  dir[2].type = TYPE_FREE;
}




/* parsing do comando e dos seus argumentos */
COMMAND parse(char *line)
{
  COMMAND com;

  com.argc = 0;
  com.argv[0] = com.cmd = strtok(line, " ");

  while( com.argv[com.argc] != NULL )
    com.argv[++com.argc] = strtok(NULL, " ");

  return com;
}




/* para cada comando invoca a função que o implementa  */
void exec_com(COMMAND com)
{

  switch( index_of(com.cmd) ) {

    case EXIT:
      exit(0);

    case MKDIR:
      if( test_argc(com.argc, 2) )
        vfs_mkdir(com.argv[1]);
     break;

    case RMDIR:
      if( test_argc(com.argc, 2) )
        vfs_rmdir(com.argv[1]);
      break;

    case CD:
      if( test_argc(com.argc, 2) )
        vfs_cd(com.argv[1]);
     break;

    case PWD:
      if( test_argc(com.argc, 1) )
        vfs_pwd();
      break;

    case LS:
      if( test_argc(com.argc, 1) )
        vfs_ls();
      break;

    case GET:
      if( test_argc(com.argc, 3) )
        vfs_get(com.argv[1], com.argv[2]);
      break;

    case PUT:
      if( test_argc(com.argc, 3) )
        vfs_put(com.argv[1], com.argv[2]);
      break;

    case CP:
      if( test_argc(com.argc, 3) )
        vfs_cp(com.argv[1], com.argv[2]);
      break;

    case MV:
      if( test_argc(com.argc, 3) )
        vfs_mv(com.argv[1], com.argv[2]);
      break;

    case RM:
      if( test_argc(com.argc, 2) )
        vfs_rm(com.argv[1]);
      break;

    case CAT:
      if( test_argc(com.argc, 2) )
        vfs_cat(com.argv[1]);
      break;

    default:
       printf("ERROR(input: command not found)\n");

  }
}




/* retorna um inteiro que representa o nome do comando */
int index_of(char *command)
{
  int i;

  for( i = 0; i < 12; i++ )
    if( strcmp(command, command_names[i]) == 0 )
      return i;

  return -1; //se o comando não é válido
}




/* testa se o número de argumentos de um comando é o esperado */
int test_argc(int argc, int expected)
{
  if( argc > expected ) {
    printf("ERROR(input: too many arguments)\n");
    return 0;
  }

  if( argc < expected ) {
    printf("ERROR(input: too few arguments)\n");
    return 0;
  }

  return 1;
}




/*-----------------------------------------------------------------------------------------------------
 Funções de manipulação de directórios
------------------------------------------------------------------------------------------------------*/

void vfs_mkdir(char *nome_dir)
{
  dir_entry *new, *aux;

  if( strchr(nome_dir, '/') ) {
    printf("ERROR(mkdir: cannot create directory `%s\' - vfs does not suport arguments with paths)\n", nome_dir);
    return;
  }

  aux = find_entry(nome_dir, dir);
  if( aux != NULL ) 
  {
    if( aux->type == TYPE_FILE )
      printf("ERROR(mkdir: cannot create direcotory `%s\' - name already used by a file)\n", nome_dir);
    else // aux->type == TYPE_DIR
      printf("ERROR(mkdir: cannot create directory `%s\' - entry exists)\n", nome_dir);

    return;
  }

  if( strlen(nome_dir) > MAX_NAME_LENGHT ) {
    printf("ERROR(mkdir: cannot create directory `%s\' - file name too long)\n", nome_dir);
    return;
  }

  new = free_entry_in(dir);
  if( new == NULL ) {
    printf("ERROR(mkdir: cannot create directory `%s\' - disk full)\n", nome_dir);
    return;
  }

  set_info( new, TYPE_DIR, nome_dir, 0, 0, 0, 0, -1 );
  set_time_cur( new );

  if( add_block_to(new) == -1 ) {
    printf("ERROR(mkdir: cannot create directory `%s\' - disk full)\n", nome_dir);
    remove_entry(new, dir);
    return;
  }

  aux = (dir_entry *)BLOCK( new->first_block );
  set_info( &aux[0], TYPE_DIR, ".", new->day, new->month, new->year, 0, new->first_block );
  set_info( &aux[1], TYPE_DIR, "..", dir->day, dir->month, dir->year, 0, dir->first_block );
  aux[2].type = TYPE_FREE;
}





void vfs_rmdir(char *nome_dir)
{
  dir_entry *entry, *aux;

  if( strchr(nome_dir, '/') ) {
    printf("ERROR(rmdir: cannot remove directory `%s\' - vfs does not suport arguments with paths)\n", nome_dir);
    return;
  }

  if( (strcmp(nome_dir, ".") == 0) || (strcmp(nome_dir, "..") == 0) ) {
    printf("ERROR(rmdir: cannot remove directory `%s\' - invalid directory)\n", nome_dir);
    return;
  }

  entry = find_entry(nome_dir, dir);

  if( entry == NULL ) {
    printf("ERROR(rmdir: cannot remove directory `%s\' - entry does not exist)\n", nome_dir);
    return;
  }

  if( entry->type == TYPE_FILE ) {
    printf("ERROR(rmdir: cannot remove `%s\' - entry is not a directory)\n", nome_dir);
    return;
  }

  aux = (dir_entry *)BLOCK( entry->first_block );

  if( aux[2].type != TYPE_FREE ) {
    printf("ERROR(rmdir: cannot remove directory `%s\' - entry not empty)\n", nome_dir);
    return;
  }

  // apaga as entradas "." e ".."
  aux[0].type = TYPE_FREE;
  aux[1].type = TYPE_FREE;

  remove_blocks_of(entry);
  remove_entry(entry, dir);
}





void vfs_cd(char *nome_dir)
{
  dir_entry *entry;

  if( strchr(nome_dir, '/') ) {
    printf("ERROR(cd: cannot move to directory `%s\' - vfs does not suport arguments with paths)\n", nome_dir);
    return;
  }

  entry = find_entry(nome_dir, dir);

  if( entry == NULL ) {
    printf("ERROR(cd: cannot move to directory `%s\' - entry does not exist)\n", nome_dir);
    return;
  }

  if( entry->type == TYPE_FILE ) {
    printf("ERROR(cd: cannot move to `%s\' - entry is not a directory)\n", nome_dir);
    return;
  }

  dir = entry;
}





void vfs_pwd(void)
{
  if( dir->first_block == sb->root_block ) 
    printf("/");
  else
    vfs_pwd_aux(dir);

  printf("\n");
}





void vfs_ls(void)
{
  int b;
  unsigned int i;
  char line[50], size[20], format[21];
  dir_entry *entry;
  LNODE *list;


  sprintf(format, "%%-%ds %%02d-%%s-%%d %%s\n", MAX_NAME_LENGHT);
  list = NULL;

  for( b = dir->first_block; b != -1; b = fat[b] ) 
  {
    entry = (dir_entry *)BLOCK( b );
    for( i = 0; i < DIR_ENTRIES(sb->block_size) && entry[i].type != TYPE_FREE; i++ )
    {

      if( entry[i].type == TYPE_DIR )
        strcpy(size, "DIR");
      else
        sprintf(size, "%d", entry[i].size);
      
      sprintf(line, format, entry[i].name, entry[i].day, months[ entry[i].month - 1 ], entry[i].year+1900, size);
      list = insert(line, list);
    }
  }

  print_list(list);
  free_list(list);
}




/*-----------------------------------------------------------------------------------------------------
 Funções de manipulação de ficheiros
------------------------------------------------------------------------------------------------------*/

void vfs_get(char *nome_orig, char *nome_dest)
{
  int n, fd, filesize, newblock;
  char op;
  dir_entry *new, *aux;

  
  if( strchr(nome_dest, '/') ) {
    printf("ERROR(get: cannot copy file `%s\' - vfs does not suport arguments with paths)\n", nome_orig);
    return;
  }

  if( (fd = open(nome_orig, O_RDONLY)) == -1 ) {
    printf("ERROR(get: cannot copy file `%s\' - unable to open file)\n", nome_orig);
    return;
  }

  aux = find_entry(nome_dest, dir);

  if( aux != NULL ) {
    if( aux->type == TYPE_DIR ) {
      printf("ERROR(get: cannot copy file `%s' - destination file name `%s' already used by a folder)\n", nome_orig, nome_dest);
      return;
    }
    else { //se já existe um ficheiro com o nome `nome_dest'
      printf("get: overwrite file `%s'? (y/N): ", nome_dest);
      op = getchar();
      getchar();
      if( op == 'y' || op == 'Y' ) {
        remove_blocks_of(aux);
        remove_entry(aux, dir);
      }
      else {
        close(fd);
        return;
      }
    }
  }

  if( strlen(nome_dest) > MAX_NAME_LENGHT ) {
    printf("ERROR(get: cannot copy file `%s\' - destination file name `%s' too long)\n", nome_orig, nome_dest);
    return;
   }

  if( (new = free_entry_in(dir)) == NULL ) {
    printf("ERROR(get: cannot copy file `%s\' - disk full)\n", nome_orig);
    return;
  }


  filesize = 0;
  set_info(new, TYPE_FILE, nome_dest, 0, 0, 0, 0, -1);
  set_time_cur(new);

  do
  {
    if( (newblock = add_block_to(new)) == -1 ) {
      printf("ERROR(get: cannot copy file `%s' - not enough space in the file system)\n", nome_orig);
      remove_blocks_of(new);
      remove_entry(new, dir);
      close(fd);
      return;
    }

    n = read(fd, BLOCK(newblock), sb->block_size);
    filesize = filesize + n;

  }
  while( n == sb->block_size );

  new->size = filesize;
  close(fd);
}





void vfs_put(char *nome_orig, char *nome_dest)
{
  int b, fd, blocksize, remain_size;
  char op;
  dir_entry *entry;

  
 if( strchr(nome_orig, '/') ) {
    printf("ERROR(put: cannot put file `%s\' - vfs does not suport arguments with paths)\n", nome_orig);
    return;
  }

  entry = find_entry(nome_orig, dir);

  if( entry == NULL ) {
    printf("ERROR(put: cannot put file `%s\' - entry does not exist)\n", nome_orig);
    return;
  }

  if( entry->type == TYPE_DIR ) {
    printf("ERROR(put: cannot put `%s\' - entry is a directory)\n", nome_orig);
    return;
  }

  // se já existe um ficheiro com o nome `nome_dest'...
  if( (fd = open(nome_dest, O_WRONLY)) != -1 )
  {
    printf("put: overwrite file `%s'? (y/N): ", nome_dest);
    op = getchar();
    getchar();
    if( op != 'y' && op != 'Y' ) {
      close(fd);
      return;
    }
  }

  //cria um novo ficheiro ou apaga o conteúdo se o ficheiro já existe
  if( (fd = open(nome_dest, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU)) == -1 ) {
    printf("ERROR(put: cannot put `%s\' - unable to create file\n", nome_orig);
    return;
  }
  
  b = entry->first_block;
  remain_size = entry->size;

  while( remain_size )
  {
    if( remain_size > sb->block_size )
      blocksize = sb->block_size;
    else
      blocksize = remain_size;

    write(fd, BLOCK(b), blocksize);
    remain_size = remain_size - blocksize;
    b = fat[b];
  }

  close(fd);
}





void vfs_cp(char *nome_orig, char *nome_dest)
{
  int  i, b, newblock, blocksize, remain_size;
  char op, *src_c, *dest_c, filename[MAX_NAME_LENGHT];
  dir_entry *src, *aux, *fileplace, *new;

  
  if( (strchr(nome_orig, '/')) || strchr(nome_dest, '/') ) {
    printf("ERROR(cp: cannot copy file `%s\' - vfs does not suport arguments with paths)\n", nome_orig);
    return;
  }

  src = find_entry(nome_orig, dir);

  if( src == NULL ) {
    printf("ERROR(cp: cannot copy file `%s\' - entry does not exist)\n", nome_orig);
    return;
  }

  if( src->type == TYPE_DIR ) {
    printf("ERROR(cp: cannot copy `%s\' - entry is a directory)\n", nome_orig);
    return;
  }

  if( strcmp(nome_orig, nome_dest) == 0 ) {
    printf("ERROR(cp: cannot copy file `%s\' - source file and destination file are the same)\n", nome_orig);
    return;
  }
  
  if( strlen(nome_dest) > MAX_NAME_LENGHT ) {
    printf("ERROR(cp: cannot copy file `%s\' - destination file name `%s' too long)\n", nome_orig, nome_dest);
    return;
  }

  strcpy(filename, nome_dest);
  fileplace = dir;

  aux = find_entry(nome_dest, dir);  
  if( aux != NULL )
  {
    if( aux->type == TYPE_DIR ) {
      strcpy(filename, nome_orig);
      fileplace = aux;
    }
    else {
      printf("cp: overwrite file `%s'? (y/N): ", nome_dest);
      op = getchar();
      getchar();
      if( op == 'y' || op == 'Y' ) {
        remove_blocks_of(aux);
        remove_entry(aux, dir);
      }
      else
        return;
    }
  }

  if( (new = free_entry_in(fileplace)) == NULL ) {
    printf("ERROR(cp: cannot copy file `%s\' - disk full)\n", nome_orig);
    return;
  }

  b = src->first_block;
  remain_size = src->size;
  set_info(new, TYPE_FILE, filename, 0, 0, 0, src->size, -1);
  set_time_cur(new);

  while( remain_size )
  {
    if( remain_size > sb->block_size )
      blocksize = sb->block_size;
    else
      blocksize = remain_size;

    if( (newblock = add_block_to(new)) ==  -1 ) {
      printf("ERROR(cp: cannot copy file `%s' - not enough space in the file system)\n", nome_orig);
      remove_blocks_of(new);
      remove_entry(new, dir);
      return;
    }

    src_c  = (char *)BLOCK( b );
    dest_c = (char *)BLOCK( newblock );
    for( i = 0; i < blocksize; i++ )
      dest_c[i] = src_c[i];

    remain_size = remain_size - blocksize;
    b = fat[b];
  }

}





void vfs_mv(char *nome_orig, char *nome_dest)
{
  char op;
  dir_entry *src, *dest, *new;


  if( (strchr(nome_orig, '/')) || strchr(nome_dest, '/') ) {
    printf("ERROR(mv: cannot move file `%s\' - vfs does not suport arguments with paths)\n", nome_orig);
    return;
  }

  src = find_entry(nome_orig, dir); 

  if( src == NULL ) {
    printf("ERROR(mv: cannot move file `%s\' - entry does not exist)\n", nome_orig);
    return;
  }

  if( src->type == TYPE_DIR ) {
    printf("ERROR(mv: cannot move `%s\' - entry is a directory)\n", nome_orig);
    return;
  }

  if( strcmp(nome_orig, nome_dest) == 0 ) {
    printf("ERROR(mv: cannot move file `%s\' - source file and destination file are the same)\n", nome_orig);
    return;
  }
  
  if( strlen(nome_dest) > MAX_NAME_LENGHT ) {
    printf("ERROR(mv: cannot move file `%s\' - destination file name `%s' too long)\n", nome_orig, nome_dest);
    return;
  }
  
  dest = find_entry(nome_dest, dir);

  // se não existe nenhuma entrada com o nome de destino --> renomear o ficheiro
  if( dest == NULL ) 
  {
    strcpy(src->name, nome_dest);
  }
  // o nome de destino é utilizado por um ficheiro
  else if( dest->type == TYPE_FILE )
  {
    printf("mv: overwrite file `%s'? (y/N): ", nome_dest);
    op = getchar();
    getchar();
    if( op == 'y' || op == 'Y' ) {
      remove_blocks_of(dest);
      remove_entry(dest, dir);
      strcpy(src->name, nome_dest);
    }
  }
  // o nome de destino é utilizado por um directório --> mover o ficheiro para o directório
  else 
  {
    if( (new = free_entry_in(dest)) == NULL ) {
      printf("ERROR(mv: cannot move file `%s\' - disk full)\n", nome_orig);  
      return;
    }
    set_info(new, TYPE_FILE, src->name, src->day, src->month, src->year, src->size, src->first_block);
    remove_entry(src, dir);
  }

}





void vfs_rm(char *nome_fich)
{
  dir_entry *entry;

  
  if( strchr(nome_fich, '/') ) {
    printf("ERROR(mkdir: cannot remove file `%s\' - vfs does not suport arguments with paths)\n", nome_fich);
    return;
  }

  entry = find_entry(nome_fich, dir);

  if( entry == NULL ) {
    printf("ERROR(rm: cannot remove file `%s\' - entry does not exist)\n", nome_fich);
    return;
  }

  if( entry->type == TYPE_DIR ) {
    printf("ERROR(rm: cannot remove `%s\' - entry is a directory)\n", nome_fich);
    return;
  }

  remove_blocks_of(entry);
  remove_entry(entry, dir);
}





void vfs_cat(char *nome_fich)
{
  int i, b, blocksize, remain_size;
  char *c;
  dir_entry *entry;


  if( strchr(nome_fich, '/') ) {
    printf("ERROR(mkdir: cannot print file `%s\' - vfs does not suport arguments with paths)\n", nome_fich);
    return;
  }

  entry = find_entry(nome_fich, dir);

  if( entry == NULL ) {
    printf("ERROR(cat: cannot print file `%s\' - entry does not exist)\n", nome_fich);
    return;
  }

  if( entry->type == TYPE_DIR ) {
    printf("ERROR(cat: cannot print `%s\' - entry is a directory)\n", nome_fich);
    return;
  }

  b = entry->first_block;
  remain_size = entry->size;

  while( remain_size )
  {
    if( remain_size > sb->block_size )
      blocksize = sb->block_size;
    else
      blocksize = remain_size;

    c = (char *)BLOCK( b );
    for( i = 0; i < blocksize; i++ )
      putchar( c[i] );

    remain_size = remain_size - blocksize;
    b = fat[b];
  }

  putchar('\n');
}




/*-----------------------------------------------------------------------------------------------------
 Funções auxiliares
------------------------------------------------------------------------------------------------------*/

/* procura a entrada `entry_name' no directório `place' e se encontra-la
 retorna um apontador para essa entrada, senão retorna NULL */
dir_entry *find_entry(char *entry_name, dir_entry *place)
{
  int b;
  unsigned int i;
  dir_entry *aux;

  // para todos os blocos ocupados pelo directório
  for( b = place->first_block; b != -1; b = fat[b] )
  {  
    aux = (dir_entry *)BLOCK( b );
    // para cada entrada do bloco
    for( i = 0; i < DIR_ENTRIES(sb->block_size) && aux[i].type != TYPE_FREE; i++)
      if( strcmp(entry_name, aux[i].name) == 0 )
        return &aux[i];

  }

  return NULL;
}





/* retorna um apontador para uma entrada livre no directório `place' */
dir_entry *free_entry_in(dir_entry* place)
{
  int b, b2;
  unsigned int i;
  dir_entry *aux, *aux2;


  b = last_block_of(place);
  aux = (dir_entry *)BLOCK( b );
    
  // para cada entrada do último bloco da lista de blocos da entrada `place'
  for( i = 0; i < DIR_ENTRIES(sb->block_size); i++)
  {
    if(aux[i].type == TYPE_FREE ) 
    {
      // se é a última entrada --> é preciso adicionar um bloco ao directório para a nova entrada livre 
      if( i == DIR_ENTRIES(sb->block_size) - 1 )
      {
        if( (b2 = add_block_to(place)) == -1 ) 
          return NULL; // o disco está cheio
     
        aux2 = (dir_entry *)BLOCK( b2 );
        aux2->type = TYPE_FREE;
      }
      // senão, a próxima entrada sera a nova entrada livre 
      else
        aux[i+1].type = TYPE_FREE;
     
      return &aux[i]; // retorna um apontador para uma entrada livre
    }
  }

   return NULL;
}





/* remove a entrada `entry' do directorio `dest' */
void remove_entry(dir_entry *entry, dir_entry *dest)
{
  int b, b_prev;
  unsigned int i;
  dir_entry *aux, *prev;
 
  for( b = dest->first_block; b != -1; b = fat[b] ) 
  {
    aux = (dir_entry *)BLOCK( b );
    for( i = 0; i < DIR_ENTRIES(sb->block_size); i++ )
    {
      if( aux[i].type == TYPE_FREE )
      {
        // a entrada a remover, passa a conter as informações da última entrada não livre do directório
        set_info(entry, prev->type, prev->name, prev->day, prev->month, prev->year, prev->size, prev->first_block);
        // a última entrada não livre passa a estar livre
        prev->type = TYPE_FREE;
        // se é a primeira entrada --> podemos remover o bloco `b' da lista de blocos ocupados pelo directório
        if( i == 0 ) 
        {
          fat[ b_prev ] = -1;
          fat[b] = sb->free_block;
          sb->free_block = b;
        }
        return;
      }
      prev = &aux[i];
    }
    b_prev = b;
  }

}





/* adiciona um bloco á entrada `d' */
int add_block_to(dir_entry *d)
{
  int b_free, b_last;

  b_free = sb->free_block;

  // se o disco esta cheio...
  if( b_free == -1 ) 
    return -1;

  sb->free_block = fat[b_free];
  fat[b_free] = -1;
  

  b_last = last_block_of(d);

  //se a entrada ainda nao contem nenhum bloco...
  if( b_last == -1 ) 
    d->first_block = b_free;
  else 
  {
    fat[b_last] = b_free;
    fat[b_free] = -1;
  }

  return b_free;
}





/* remove os blocos ocupados pela entrada `d' e adiciona-os à lista de blocos livres */
void remove_blocks_of(dir_entry *d) 
{
  int b;

  b = last_block_of(d);
  fat[b] = sb->free_block;
  sb->free_block = d->first_block;
}





/* retorna o último bloco da lista de blocos ocupados pela entrada `d' */
int last_block_of(dir_entry *d) 
{
  int b;

  b = d->first_block;

  // se o directorio nao tem nenhum bloco...
  if( b == -1 )
    return -1;

  while( fat[b] != -1 )
    b = fat[b];  

  return b;
}





/* preenche os campos de informação da entrada `d' */
void set_info(dir_entry *d, char type, char *name,
     unsigned char day, unsigned char month, unsigned char year, int size, int first_block)
{
  d->type = type;
  strcpy(d->name, name);
  d->day   = day;
  d->month = month;
  d->year  = year;
  d->size  = size;
  d->first_block = first_block;
}





/* preenche os campos de tempo da entrada `d' com o tempo actual */
void set_time_cur(dir_entry *d)
{
  time_t cur_time;
  struct tm *cur_tm;

  cur_time = time(NULL);
  cur_tm = localtime( &cur_time );

  d->day   = cur_tm->tm_mday;
  d->month = cur_tm->tm_mon + 1;
  d->year  = cur_tm->tm_year;
}





void vfs_pwd_aux(dir_entry *entry)
{
  int b;
  unsigned int i;
  dir_entry *aux;


  /* se o directorio é a raiz --> regressa */
  if( entry->first_block == sb->root_block )
    return;

  aux = (dir_entry *)BLOCK( entry->first_block );
  vfs_pwd_aux(&aux[1]); // entrada ".." de `entry'

  for( b = aux[1].first_block; b != -1; b = fat[b] ) 
  {
    aux = (dir_entry *)BLOCK( b );
    for( i = 0; i < DIR_ENTRIES(sb->block_size); i++)
      if( aux[i].first_block == entry->first_block )
      {
        printf("/%s", aux[i].name);
        return;
      }
 
  }
}





/* insere ordenado alfabeticamente um elemento na lista */
LNODE* insert(char *data, LNODE *list)
{
  LNODE *p;
  LNODE *q;

  /* cria um novo no */
  p = (LNODE *)malloc( sizeof(LNODE) );
  /* guarda a `data' no novo nó */
  p->str = strdup(data);

  /* casos em que a `data' deve ser o 1º elemento */
  if( list == NULL || strcmp(list->str, data) > 0 )
  {
    p->next = list;
    return p;
  } 
  else
  {
    /* procura na lista o sitio certo para colocar a `data' */
    q = list;
    while( q->next != NULL && strcmp(q->next->str, data) < 0 )
      q = q->next;

    p->next = q->next;
    q->next = p;
    return list;
  }
}





/* imprime as strings guardadas na lista */  
void print_list(LNODE *list)
{
  LNODE *p;

  for( p = list; p != NULL; p = p->next )
    printf("%s", p->str);

}





/* liberta da memória todos os nós da lista */
void free_list(LNODE *list) 
{
 LNODE *p;

 while( list != NULL ) {
   p = list->next;
   free(list);
   list = p;
 }
}

