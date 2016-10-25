#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char *get_current_dir_name();

char *cmd_line = NULL;
char **g_env;
size_t len = 256;

struct command {
  char *path;
  char *args[5];
  char infile[50];
  char outfile[50];
  bool append;
};

struct command cmds[2];

void prompt() { printf("you@(%s) $: ", get_current_dir_name()); }

void do_exec(struct command *cmd) {
  if (strcmp(cmd->outfile, "") != 0) {
    close(1);
    if (cmd->append) {
      open(cmd->outfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
    } else {
      open(cmd->outfile, O_WRONLY | O_CREAT, 0644);
    }
  }
  if (strcmp(cmd->infile, "") != 0) {
    close(0);
    open(cmd->infile, O_RDONLY);
  }
  execve(cmd->path, (char *const *)&cmd->args, g_env);
}

void do_exec_pipe(struct command *cmd) {
  int pd[2];
  pipe(pd);

  pid_t pid = fork();

  if (pid) {
    close(pd[0]);
    dup2(pd[1], 1);
    close(pd[1]);

    do_exec(&cmd[0]);
  } else {
    close(pd[1]);
    dup2(pd[0], 0);
    close(pd[0]);

    do_exec(&cmd[1]);
  }
}

void build_cmd(struct command *cmd, char **tokens, int start, int end) {
  int arg_count = 0;
  cmd->args[0] = cmd->path;
  arg_count++;
  for (int i = start; i < end; i++) {
    if (strcmp(tokens[i], "<") == 0) {
      strcpy(cmd->infile, tokens[i + 1]);
      i++;
    } else if (strcmp(tokens[i], ">") == 0) {
      strcpy(cmd->outfile, tokens[i + 1]);
      i++;
    } else if (strcmp(tokens[i], ">>") == 0) {
      strcpy(cmd->outfile, tokens[i + 1]);
      cmd->append = true;
      i++;
    } else if (i != start) {
      cmd->args[arg_count] = tokens[i];
      arg_count++;
    }
  }
  cmd->args[arg_count + 1] = 0;
}

int tokenize(char *line, char **argv, char *delim) {
  int ncmds = 0;
  char *cur_cmd = strtok(line, delim);

  while (cur_cmd) {
    argv[ncmds++] = cur_cmd;
    cur_cmd = strtok(NULL, delim);
  }

  return ncmds;
}

char *find_bin(char *cmd) {
  char *path_env = getenv("PATH");
  static char buf[256] = {0};
  strcpy(buf, path_env);

  static char *tokens[32];
  int num_paths = tokenize(buf, tokens, ":");

  int i, found = 0;
  struct stat dummy;
  char *full_path;
  for (i = 0; i < num_paths; i++) {
    full_path = malloc(strlen(tokens[i]) + strlen(cmd) + 2);
    strcpy(full_path, tokens[i]);
    strcat(full_path, "/");
    strcat(full_path, cmd);
    if (stat(full_path, &dummy) == 0) {
      found = 1;
      break;
    }
    free(full_path);
  }
  return full_path;
}

void parseExec(char *line) {
  static char buf[256] = {0};
  strcpy(buf, line);
  memset(cmds, 0, sizeof(cmds) * 2);
  static char *tokens[32];
  int argc = tokenize(buf, tokens, " ");

  if (argc > 0) {
    // strip newline on last arg
    tokens[argc - 1][strlen(tokens[argc - 1]) - 1] = 0;
    // builtins
    if (strcmp(tokens[0], "quit") == 0) exit(0);
    if (strcmp(tokens[0], "cd") == 0) {
      argc > 1 ? chdir(tokens[1]) : chdir(getenv("HOME"));
      return;
    }
    cmds[0].path = find_bin(tokens[0]);

    // check for pipe and build 2nd cmd
    int ispipe = 0;
    int i;
    for (i = 0; i < argc; i++) {
      if (strcmp(tokens[i], "|") == 0) {
        cmds[1].path = find_bin(tokens[i + 1]);
        ispipe = 1;
        break;
      }
    }

    if (ispipe) {
      // we have to set cmds[0] and cmds[1] args and fds
      build_cmd(&cmds[0], tokens, 0, i);
      build_cmd(&cmds[1], tokens, i + 1, argc);

      int status;
      pid_t pid = fork();
      if (pid) {
        usleep(10000);  // 10ms
        wait(&status);
      } else {
        do_exec_pipe(cmds);
      }
    } else {
      // only cmd[0] is to be set
      build_cmd(&cmds[0], tokens, 0, argc);
      int status;
      pid_t pid = fork();
      if (pid) {
        wait(&status);
      } else {
        do_exec(&cmds[0]);
      }
    }
  }
}

int main(int argc, char *argv[], char *env[]) {
  g_env = env;

  while (1) {
    prompt();
    getline(&cmd_line, &len, stdin);
    parseExec(cmd_line);
  }
}
