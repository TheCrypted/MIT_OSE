#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Simplifed xv6 shell.

#define MAXARGS 10

// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type;          //  ' ' (exec), | (pipe), '<' or '>' for redirection
};

struct execcmd {
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};

struct redircmd {
  int type;          // < or >
  struct cmd *cmd;   // the command to be run (e.g., an execcmd)
  char *file;        // the input/output file
  int mode;          // the mode to open the file with
  int fd;            // the file descriptor number to use for the file
};

struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe
  struct cmd *right; // right side of pipe
};

struct listcmd {
  int type;          // ;
  struct cmd *left;  // left side of list
  struct cmd *right; // right side of list
};

struct subcmd {
  int type;          // (
  struct cmd *scmd;
};


int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2], r;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct listcmd *lcmd;
  struct subcmd *scmd;

  if(cmd == 0)
    return;

  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    return;

  case ' ':
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      return;

    if(strcmp(ecmd->argv[0], "cd") == 0){
      int count = 0;
      while(ecmd->argv[count] != NULL)
        count++;
      if (count != 2)
      {
        fprintf(stderr, "incorrect number of argument for cd\n");
        return;
      }
      char* buf = ecmd->argv[1];
      if(chdir(ecmd->argv[1]) < 0)
        fprintf(stderr, "cannot cd \"%s\"\n", buf);
      return;
    }

    if (fork1() == 0)
    {
      execvp(ecmd->argv[0], ecmd->argv);
      perror("execvp");
      exit(0);
    }
    wait(NULL);
    return;

  case '>':
  case '<':
    rcmd = (struct redircmd*)cmd;
    int rid = fork1();
    if (rid == 0)
    {
      int file_fd = open(rcmd->file, rcmd->mode, 0644);
      if(file_fd < 0){
        perror("open");
        return;
      }

      dup2(file_fd, rcmd->fd);
      close(file_fd);
      runcmd(rcmd->cmd);
      exit(0);
    }
    wait(NULL);
    return;

  case '|':
    pcmd = (struct pipecmd*)cmd;

    int p[2];
    if (pipe(p) < 0)
    {
      perror("pipe");
      return;
    }

    int pid2 = fork();
    if (pid2 == 0)
    {
      close(p[0]);
      dup2(p[1], STDOUT_FILENO);
      close(p[1]);
      runcmd(pcmd->left);
      exit(0);
    }

    int pid = fork();
    if (pid == 0)
    {
      dup2(p[0], STDIN_FILENO);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
      exit(0);
    }
    close(p[0]);
    close(p[1]);

    wait(NULL);
    wait(NULL);
    return;

  case ';':
    lcmd = (struct listcmd*)cmd;

    runcmd(lcmd->left);
    runcmd(lcmd->right);
    return;

  case '(':
    scmd = (struct subcmd*)cmd;
    int sid = fork1();
    if (sid == 0)
    {
      runcmd(scmd->scmd);
      exit(0);
    }
    wait(NULL);
    return;
  }



}

int
getcmd(char *buf, int nbuf)
{

  if (isatty(fileno(stdin)))
    fprintf(stdout, "$ ");
  memset(buf, 0, nbuf);
  fgets(buf, nbuf, stdin);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd, r;

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    // if(fork1() == 0)
    runcmd(parsecmd(buf));
    // wait(&r);
  }
  exit(0);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, int type)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->mode = (type == '<') ?  O_RDONLY : O_WRONLY|O_CREAT|O_TRUNC;
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ';';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
subcmd(struct cmd* scmd)
{
  struct subcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '(';
  cmd->scmd = scmd;
  return (struct cmd*)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
    case 0:
      break;
    case '|':
    case ';':
    case '<':
    case '>':
      s++;
      break;
    case '(':
      ret = '(';
      s++;
      break;
    case ')':
      ret = ')';
      s++;
      break;
    default:
      ret = 'a';
      while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
        s++;
      break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parselist(char**, char*);
struct cmd *parseexec(char**, char*);

// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char
*mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;
  cmd = parselist(ps, es);
  // cmd = parsepipe(ps, es);
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);

  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parselist(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  // struct execcmd *ecmd = (struct execcmd*)cmd;
  // int count = 0;
  // while(ecmd->argv[count] != NULL)
  //   count++;
  // printf("command identified: %s, size: %d\n", ecmd->argv[0], count);
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);

    cmd = listcmd(cmd, parselist(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a') {
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    switch(tok){
    case '<':
      cmd = redircmd(cmd, mkcopy(q, eq), '<');
      break;
    case '>':
      cmd = redircmd(cmd, mkcopy(q, eq), '>');
      break;
    }
  }
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;

  ret = parseredirs(ret, ps, es);

  if (peek(ps, es, "("))
  {
    gettoken(ps, es, 0, 0);
    struct cmd* lcmd = parseline(ps, es);
    if (ret->type == ' ')
      ret = subcmd(lcmd);
    else
    {
      struct cmd* curr = ret;
      while (curr->type == '<' || curr->type == '>') {
        struct redircmd *r = (struct redircmd*)curr;

        if (r->cmd->type != '<' && r->cmd->type != '>')
          break;

        curr = r->cmd;
      }
      ((struct redircmd*)curr)->cmd = subcmd(lcmd);
    }

    if (!peek(ps, es, ")"))
    {
      fprintf(stderr, "syntax error: bracket not closed");
      exit(-1);
    }
    gettoken(ps, es, 0, 0);
    ret = parseredirs(ret, ps, es);
    return ret;
  }

  while(!peek(ps, es, "|;()")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  return ret;
}