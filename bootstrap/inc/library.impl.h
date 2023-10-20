#include <stdio.h>
#include <stdlib.h>

void fn__hexout_(t$i32 x);

void fn_writes(t$i32 fd, t$str s);
void fn_writex(t$i32 fd, t$i32 n);
void fn_writei(t$i32 fd, t$i32 n);
void fn_writec(t$i32 fd, t$i32 c);
t$i32 fn_readc(t$i32 fd);

t$i32 fn_fd_open(t$str s);
t$i32 fn_fd_create(t$str s);
void fn_fd_close(t$i32 fd);
t$i32 fn_fd_set_pos(t$i32 fd, t$u32 pos);
t$u32 fn_fd_get_pos(t$i32 fd);

t$u8* fn_os_arg(t$i32 n);
t$i32 fn_os_arg_count(void);
void fn_os_exit(t$i32 n);
void fn_abort(void);
