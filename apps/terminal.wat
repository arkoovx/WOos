(module
  ;; Imports from env
  (import "env" "woos_graphics_create_window_ex" (func $create_window_ex (param i32 i32 i32 i32) (result i32)))
  (import "env" "woos_graphics_blit_window" (func $blit_window (param i32 i32 i32 i32) (result i32)))
  (import "env" "woos_graphics_draw_text_to_buffer" (func $draw_text_raw (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "woos_ipc_recv" (func $ipc_recv (param i32) (result i32)))
  (import "env" "woos_ipc_wait_message" (func $wait_message (result i32)))
  (import "env" "woos_system_spawn" (func $system_spawn (param i32) (result i32)))
  (import "env" "woos_system_get_status" (func $get_status (param i32) (result i32)))
  (import "env" "woos_net_resolve" (func $net_resolve (param i32 i32 i32) (result i32)))
  (import "env" "woos_net_get_config" (func $net_get_config (param i32 i32) (result i32)))

  ;; Socket Imports
  (import "env" "woos_socket_create" (func $sock_create (param i32) (result i32)))
  (import "env" "woos_socket_connect" (func $sock_connect (param i32 i32 i32) (result i32)))
  (import "env" "woos_socket_send" (func $sock_send (param i32 i32 i32) (result i32)))
  (import "env" "woos_socket_recv" (func $sock_recv (param i32 i32 i32) (result i32)))
  (import "env" "woos_socket_close" (func $sock_close (param i32) (result i32)))

  ;; WASI Imports
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (import "wasi_snapshot_preview1" "path_open" (func $path_open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read" (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $fd_close (param i32) (result i32)))

  ;; Linear Memory: 16 pages (1 MB)
  (memory (export "memory") 16)

  ;; Constants and String Data (Top-Level)
  (data (i32.const 500) "Terminal\00")
  (data (i32.const 520) "woos# \00")
  (data (i32.const 540) "WoOS Terminal (v1.44.0)\00")
  (data (i32.const 590) "Type 'help' for available commands.\00")
  (data (i32.const 640) "WoOS 1.44.0 #1 SMP PREEMPT x86_64 WoOS Microkernel\00")
  (data (i32.const 710) "Starting /CALC.WAS...\00")
  (data (i32.const 740) "woos: command not found: \00")
  (data (i32.const 780) "/CALC.WAS\00")

  (data (i32.const 800) "help\00")
  (data (i32.const 810) "uname\00")
  (data (i32.const 820) "uptime\00")
  (data (i32.const 830) "clear\00")
  (data (i32.const 840) "ls\00")
  (data (i32.const 850) "dir\00")
  (data (i32.const 860) "cat\00")
  (data (i32.const 870) "echo\00")
  (data (i32.const 880) "free\00")
  (data (i32.const 890) "ps\00")
  (data (i32.const 900) "ifconfig\00")
  (data (i32.const 910) "ip\00")
  (data (i32.const 920) "ping\00")
  (data (i32.const 930) "fetch\00")
  (data (i32.const 940) "calc\00")
  (data (i32.const 950) "exit\00")
  (data (i32.const 960) "notepad\00")
  (data (i32.const 970) "edit\00")
  (data (i32.const 980) "/NOTEPAD.WAS\00")
  (data (i32.const 1000) "Starting /NOTEPAD.WAS...\00")
  (data (i32.const 1030) "explorer\00")
  (data (i32.const 1040) "/EXPLORER.WAS\00")
  (data (i32.const 1060) "Starting /EXPLORER.WAS...\00")

  ;; Files list for ls
  (data (i32.const 1100) "HELLO.TXT     README.TXT    COMPOSIT.WAS  CALC.WAS      TERM.WAS      NOTEPAD.WAS   EXPLORER.WAS\00")

  ;; Help text lines
  (data (i32.const 1200) "Available commands:\00")
  (data (i32.const 1230) "  help        - Display this help message\00")
  (data (i32.const 1280) "  uname [-a]  - Print system and kernel info\00")
  (data (i32.const 1330) "  uptime      - Display system running time\00")
  (data (i32.const 1380) "  ls / dir    - List files on VFS storage\00")
  (data (i32.const 1430) "  cat <file>  - Display file contents\00")
  (data (i32.const 1480) "  echo <text> - Print text to console\00")
  (data (i32.const 1530) "  clear       - Clear terminal screen\00")
  (data (i32.const 1580) "  free        - Show memory usage statistics\00")
  (data (i32.const 1630) "  ps          - Report active processes\00")
  (data (i32.const 1680) "  ifconfig    - Show network configuration\00")
  (data (i32.const 1730) "  ping <host> - DNS resolution and ping probe\00")
  (data (i32.const 1780) "  fetch <url> - Fetch web page via HTTP client\00")
  (data (i32.const 1830) "  calc        - Launch Calculator application\00")
  (data (i32.const 1870) "  notepad     - Launch Notepad text editor\00")
  (data (i32.const 1910) "  explorer    - Launch Windows File Explorer\00")
  (data (i32.const 1950) "  exit        - Close terminal\00")

  ;; PS header
  (data (i32.const 1930) "  PID TTY          TIME CMD\00")
  (data (i32.const 1970) "    1 ?        00:00:00 [idle]\00")
  (data (i32.const 2010) "    2 ?        00:00:06 compositor\00")
  (data (i32.const 2050) "    3 ?        00:00:01 terminal\00")

  ;; Free headers
  (data (i32.const 2100) "               total        used        free\00")
  (data (i32.const 2150) "Mem:          128 MB       14 MB      114 MB\00")
  (data (i32.const 2200) "Heap:           8 MB        2 MB        6 MB\00")

  ;; HTTP request
  (data (i32.const 2250) "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n\00")

  ;; Ping and fetch messages
  (data (i32.const 3000) "Usage: ping <host|ip>\00")
  (data (i32.const 3030) "ping: unknown host \00")
  (data (i32.const 3060) "PING \00")
  (data (i32.const 3070) " (\00")
  (data (i32.const 3080) "): 56 data bytes\00")
  (data (i32.const 3110) "64 bytes from \00")
  (data (i32.const 3130) ": icmp_seq=1 ttl=64 time=1 ms\00")
  (data (i32.const 3170) ": icmp_seq=2 ttl=64 time=1 ms\00")
  (data (i32.const 3210) "--- ping statistics ---\00")
  (data (i32.const 3240) "2 packets transmitted, 2 received, 0% packet loss\00")
  (data (i32.const 3300) "Usage: fetch <host|ip>\00")
  (data (i32.const 3330) "fetch: socket creation failed\00")
  (data (i32.const 3370) "fetch: connected, no data returned\00")
  (data (i32.const 3420) "Usage: cat <filename>\00")
  (data (i32.const 3450) "cat: cannot open file: \00")

  ;; Globals
  (global $g_win_id (mut i32) (i32.const 0))
  (global $g_cursor_row (mut i32) (i32.const 0))
  (global $g_cursor_col (mut i32) (i32.const 0))
  (global $g_input_len (mut i32) (i32.const 0))

  ;; Memory Layout:
  ;; 10000..11760: 22 lines x 80 columns character grid (1760 bytes)
  ;; 12000..12100: current command input buffer (100 bytes)
  ;; 13000..15000: scratch buffer (2000 bytes)
  ;; 15000..15032: iovec struct for WASI fd_read
  ;; 16000..16100: IP string buffer for DNS resolve
  ;; 65536..987136: 640x360x4 ARGB pixel buffer (921,600 bytes)

  ;; Memory copy
  (func $memcpy (param $dst i32) (param $src i32) (param $len i32)
    (local $i i32)
    i32.const 0
    local.set $i
    (block $break_cpy
      (loop $loop_cpy
        local.get $i
        local.get $len
        i32.ge_u
        br_if $break_cpy

        local.get $dst
        local.get $i
        i32.add
        local.get $src
        local.get $i
        i32.add
        i32.load8_u
        i32.store8

        local.get $i
        i32.const 1
        i32.add
        local.set $i
        br $loop_cpy
      )
    )
  )

  ;; Memory set
  (func $memset (param $dst i32) (param $val i32) (param $len i32)
    (local $i i32)
    i32.const 0
    local.set $i
    (block $break_set
      (loop $loop_set
        local.get $i
        local.get $len
        i32.ge_u
        br_if $break_set

        local.get $dst
        local.get $i
        i32.add
        local.get $val
        i32.store8

        local.get $i
        i32.const 1
        i32.add
        local.set $i
        br $loop_set
      )
    )
  )

  ;; String length
  (func $strlen (param $s i32) (result i32)
    (local $len i32)
    i32.const 0
    local.set $len
    (block $break_len
      (loop $loop_len
        local.get $s
        local.get $len
        i32.add
        i32.load8_u
        i32.eqz
        br_if $break_len

        local.get $len
        i32.const 1
        i32.add
        local.set $len
        br $loop_len
      )
    )
    local.get $len
  )

  ;; String equality
  (func $streq (param $s1 i32) (param $s2 i32) (result i32)
    (local $c1 i32)
    (local $c2 i32)
    (loop $loop_cmp
      local.get $s1
      i32.load8_u
      local.set $c1

      local.get $s2
      i32.load8_u
      local.set $c2

      local.get $c1
      local.get $c2
      i32.ne
      if
        i32.const 0
        return
      end

      local.get $c1
      i32.eqz
      if
        i32.const 1
        return
      end

      local.get $s1
      i32.const 1
      i32.add
      local.set $s1

      local.get $s2
      i32.const 1
      i32.add
      local.set $s2
      br $loop_cmp
    )
    i32.const 0
  )

  ;; String starts with
  (func $startswith (param $str i32) (param $prefix i32) (result i32)
    (local $cp i32)
    (loop $loop_sw
      local.get $prefix
      i32.load8_u
      local.set $cp

      local.get $cp
      i32.eqz
      if
        i32.const 1
        return
      end

      local.get $str
      i32.load8_u
      local.get $cp
      i32.ne
      if
        i32.const 0
        return
      end

      local.get $str
      i32.const 1
      i32.add
      local.set $str

      local.get $prefix
      i32.const 1
      i32.add
      local.set $prefix
      br $loop_sw
    )
    i32.const 0
  )

  ;; Clear terminal screen
  (func $term_clear
    i32.const 10000
    i32.const 32 ;; space
    i32.const 1760
    call $memset

    i32.const 0
    global.set $g_cursor_row
    i32.const 0
    global.set $g_cursor_col
    i32.const 0
    global.set $g_input_len
  )

  ;; Scroll terminal text up by 1 line
  (func $term_scroll_up
    i32.const 10000
    i32.const 10080
    i32.const 1680
    call $memcpy

    i32.const 11680
    i32.const 32
    i32.const 80
    call $memset

    i32.const 21
    global.set $g_cursor_row
    i32.const 0
    global.set $g_cursor_col
  )

  ;; Move to next line
  (func $term_newline
    i32.const 0
    global.set $g_cursor_col

    global.get $g_cursor_row
    i32.const 1
    i32.add
    global.set $g_cursor_row

    global.get $g_cursor_row
    i32.const 22
    i32.ge_s
    if
      call $term_scroll_up
    end
  )

  ;; Put single character
  (func $term_putc (param $c i32)
    ;; Handle Newline (\n = 10)
    local.get $c
    i32.const 10
    i32.eq
    if
      call $term_newline
      return
    end

    ;; Handle Carriage Return (\r = 13)
    local.get $c
    i32.const 13
    i32.eq
    if
      i32.const 0
      global.set $g_cursor_col
      return
    end

    ;; Handle Backspace (\b = 8)
    local.get $c
    i32.const 8
    i32.eq
    if
      global.get $g_cursor_col
      i32.const 0
      i32.gt_s
      if
        global.get $g_cursor_col
        i32.const 1
        i32.sub
        global.set $g_cursor_col

        ;; Clear cell
        i32.const 10000
        global.get $g_cursor_row
        i32.const 80
        i32.mul
        i32.add
        global.get $g_cursor_col
        i32.add
        i32.const 32 ;; space
        i32.store8
      end
      return
    end

    ;; Printable ASCII [32..126]
    local.get $c
    i32.const 32
    i32.ge_s
    local.get $c
    i32.const 126
    i32.le_s
    i32.and
    if
      ;; Wrap if line end reached
      global.get $g_cursor_col
      i32.const 80
      i32.ge_s
      if
        call $term_newline
      end

      ;; Store character into grid
      i32.const 10000
      global.get $g_cursor_row
      i32.const 80
      i32.mul
      i32.add
      global.get $g_cursor_col
      i32.add
      local.get $c
      i32.store8

      global.get $g_cursor_col
      i32.const 1
      i32.add
      global.set $g_cursor_col
    end
  )

  ;; Put string
  (func $term_puts (param $s i32)
    (local $c i32)
    (loop $loop_s
      local.get $s
      i32.load8_u
      local.set $c

      local.get $c
      i32.eqz
      if
        return
      end

      local.get $c
      call $term_putc

      local.get $s
      i32.const 1
      i32.add
      local.set $s
      br $loop_s
    )
  )

  ;; Redraw terminal screen into 640x360 buffer and blit
  (func $term_render
    (local $row i32)
    (local $cur_y i32)
    (local $px i32)
    (local $cx i32)
    (local $cy i32)
    (local $cpx i32)
    (local $cpy i32)

    ;; 1. Clear 640x360 pixel buffer at 65536 with dark console color 0x121212
    i32.const 65536
    local.set $px
    (block $break_clr
      (loop $loop_clr
        local.get $px
        i32.const 987136 ;; 65536 + 921600
        i32.ge_u
        br_if $break_clr

        local.get $px
        i32.const 0x121212
        i32.store

        local.get $px
        i32.const 4
        i32.add
        local.set $px
        br $loop_clr
      )
    )

    ;; 2. Render each of 22 lines using draw_text_raw
    i32.const 0
    local.set $row
    (block $break_rows
      (loop $loop_rows
        local.get $row
        i32.const 22
        i32.ge_s
        br_if $break_rows

        ;; Copy 80 chars of this line into scratch area 14000 and null-terminate it
        i32.const 14000
        i32.const 10000
        local.get $row
        i32.const 80
        i32.mul
        i32.add
        i32.const 80
        call $memcpy

        i32.const 14080
        i32.const 0
        i32.store8

        ;; Draw line text at y = 4 + row * 16
        i32.const 4
        local.get $row
        i32.const 16
        i32.mul
        i32.add
        local.set $cur_y

        i32.const 65536 ;; dst_buf
        i32.const 640   ;; dst_w
        i32.const 360   ;; dst_h
        i32.const 4     ;; x
        local.get $cur_y ;; y
        i32.const 14000 ;; text ptr
        i32.const 0xD8D8D8 ;; light silver text color
        call $draw_text_raw
        drop

        local.get $row
        i32.const 1
        i32.add
        local.set $row
        br $loop_rows
      )
    )

    ;; 3. Render Cursor Block in green (0x2ECC71) at (4 + g_cursor_col * 8, 4 + g_cursor_row * 16 + 13, 8, 3)
    i32.const 4
    global.get $g_cursor_col
    i32.const 8
    i32.mul
    i32.add
    local.set $cx

    i32.const 4
    global.get $g_cursor_row
    i32.const 16
    i32.mul
    i32.add
    i32.const 13
    i32.add
    local.set $cy

    i32.const 0
    local.set $cpy
    (block $break_cy
      (loop $loop_cy
        local.get $cpy
        i32.const 3
        i32.ge_s
        br_if $break_cy

        i32.const 0
        local.set $cpx
        (block $break_cx
          (loop $loop_cx
            local.get $cpx
            i32.const 8
            i32.ge_s
            br_if $break_cx

            ;; Pixel at (cx + cpx, cy + cpy)
            i32.const 65536
            local.get $cy
            local.get $cpy
            i32.add
            i32.const 640
            i32.mul
            local.get $cx
            local.get $cpx
            i32.add
            i32.add
            i32.const 4
            i32.mul
            i32.add
            i32.const 0x2ECC71 ;; bright green
            i32.store

            local.get $cpx
            i32.const 1
            i32.add
            local.set $cpx
            br $loop_cx
          )
        )

        local.get $cpy
        i32.const 1
        i32.add
        local.set $cpy
        br $loop_cy
      )
    )

    ;; 4. Blit window buffer
    global.get $g_win_id
    i32.const 65536
    i32.const 640
    i32.const 360
    call $blit_window
    drop
  )

  ;; Command Executor
  (func $term_exec_cmd
    (local $cmd_ptr i32)
    (local $arg_ptr i32)
    (local $fd i32)
    (local $read_bytes i32)
    (local $sock i32)

    ;; Command string is at 12000
    i32.const 12000
    local.set $cmd_ptr

    ;; If empty command, just print newline
    local.get $cmd_ptr
    i32.load8_u
    i32.eqz
    if
      call $term_newline
      return
    end

    call $term_newline

    ;; Command: help
    local.get $cmd_ptr
    i32.const 800 ;; "help"
    call $streq
    if
      i32.const 1200
      call $term_puts
      call $term_newline
      i32.const 1230
      call $term_puts
      call $term_newline
      i32.const 1280
      call $term_puts
      call $term_newline
      i32.const 1330
      call $term_puts
      call $term_newline
      i32.const 1380
      call $term_puts
      call $term_newline
      i32.const 1430
      call $term_puts
      call $term_newline
      i32.const 1480
      call $term_puts
      call $term_newline
      i32.const 1530
      call $term_puts
      call $term_newline
      i32.const 1580
      call $term_puts
      call $term_newline
      i32.const 1630
      call $term_puts
      call $term_newline
      i32.const 1680
      call $term_puts
      call $term_newline
      i32.const 1730
      call $term_puts
      call $term_newline
      i32.const 1780
      call $term_puts
      call $term_newline
      i32.const 1830
      call $term_puts
      call $term_newline
      i32.const 1880
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: uname
    local.get $cmd_ptr
    i32.const 810 ;; "uname"
    call $startswith
    if
      i32.const 640 ;; WoOS 1.41.0 ...
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: clear
    local.get $cmd_ptr
    i32.const 830 ;; "clear"
    call $streq
    if
      call $term_clear
      i32.const 540 ;; Banner
      call $term_puts
      call $term_newline
      i32.const 590 ;; Type 'help'...
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: ls or dir
    local.get $cmd_ptr
    i32.const 840 ;; "ls"
    call $streq
    local.get $cmd_ptr
    i32.const 850 ;; "dir"
    call $streq
    i32.or
    if
      i32.const 1100 ;; Files list
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: echo
    local.get $cmd_ptr
    i32.const 870 ;; "echo"
    call $startswith
    if
      local.get $cmd_ptr
      i32.const 4
      i32.add
      local.set $arg_ptr
      (loop $loop_sk
        local.get $arg_ptr
        i32.load8_u
        i32.const 32
        i32.eq
        if
          local.get $arg_ptr
          i32.const 1
          i32.add
          local.set $arg_ptr
          br $loop_sk
        end
      )
      local.get $arg_ptr
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: free
    local.get $cmd_ptr
    i32.const 880 ;; "free"
    call $streq
    if
      i32.const 2100
      call $term_puts
      call $term_newline
      i32.const 2150
      call $term_puts
      call $term_newline
      i32.const 2200
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: ps
    local.get $cmd_ptr
    i32.const 890 ;; "ps"
    call $streq
    if
      i32.const 1930
      call $term_puts
      call $term_newline
      i32.const 1970
      call $term_puts
      call $term_newline
      i32.const 2010
      call $term_puts
      call $term_newline
      i32.const 2050
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: ifconfig or ip
    local.get $cmd_ptr
    i32.const 900 ;; "ifconfig"
    call $startswith
    local.get $cmd_ptr
    i32.const 910 ;; "ip"
    call $startswith
    i32.or
    if
      i32.const 13000
      i32.const 1024
      call $net_get_config
      drop

      i32.const 13000
      call $term_puts
      return
    end

    ;; Command: ping <host>
    local.get $cmd_ptr
    i32.const 920 ;; "ping"
    call $startswith
    if
      local.get $cmd_ptr
      i32.const 4
      i32.add
      local.set $arg_ptr
      (loop $loop_sp
        local.get $arg_ptr
        i32.load8_u
        i32.const 32
        i32.eq
        if
          local.get $arg_ptr
          i32.const 1
          i32.add
          local.set $arg_ptr
          br $loop_sp
        end
      )

      local.get $arg_ptr
      i32.load8_u
      i32.eqz
      if
        i32.const 3000
        call $term_puts
        call $term_newline
        return
      end

      ;; Resolve host
      local.get $arg_ptr
      i32.const 16000 ;; out_ip
      i32.const 64
      call $net_resolve
      local.set $fd

      local.get $fd
      i32.const 0
      i32.ne
      if
        i32.const 3030
        call $term_puts
        local.get $arg_ptr
        call $term_puts
        call $term_newline
        return
      end

      ;; Print ping stats
      i32.const 3060
      call $term_puts
      local.get $arg_ptr
      call $term_puts
      i32.const 3070
      call $term_puts
      i32.const 16000
      call $term_puts
      i32.const 3080
      call $term_puts
      call $term_newline

      i32.const 3110
      call $term_puts
      i32.const 16000
      call $term_puts
      i32.const 3130
      call $term_puts
      call $term_newline

      i32.const 3110
      call $term_puts
      i32.const 16000
      call $term_puts
      i32.const 3170
      call $term_puts
      call $term_newline

      i32.const 3210
      call $term_puts
      call $term_newline
      i32.const 3240
      call $term_puts
      call $term_newline
      return
    end

    ;; Command: fetch <ip>
    local.get $cmd_ptr
    i32.const 930 ;; "fetch"
    call $startswith
    if
      local.get $cmd_ptr
      i32.const 5
      i32.add
      local.set $arg_ptr
      (loop $loop_sf
        local.get $arg_ptr
        i32.load8_u
        i32.const 32
        i32.eq
        if
          local.get $arg_ptr
          i32.const 1
          i32.add
          local.set $arg_ptr
          br $loop_sf
        end
      )

      local.get $arg_ptr
      i32.load8_u
      i32.eqz
      if
        i32.const 3300
        call $term_puts
        call $term_newline
        return
      end

      ;; Create TCP socket (type 1)
      i32.const 1
      call $sock_create
      local.set $sock

      local.get $sock
      i32.const 0
      i32.lt_s
      if
        i32.const 3330
        call $term_puts
        call $term_newline
        return
      end

      ;; Connect to port 80
      local.get $sock
      local.get $arg_ptr
      i32.const 80
      call $sock_connect
      drop

      ;; Send HTTP GET
      local.get $sock
      i32.const 2250 ;; GET / HTTP/1.0
      i32.const 39
      call $sock_send
      drop

      ;; Receive response
      local.get $sock
      i32.const 13000
      i32.const 500
      call $sock_recv
      local.set $read_bytes

      local.get $read_bytes
      i32.const 0
      i32.gt_s
      if
        i32.const 13000
        local.get $read_bytes
        i32.add
        i32.const 0
        i32.store8

        i32.const 13000
        call $term_puts
        call $term_newline
      else
        i32.const 3370
        call $term_puts
        call $term_newline
      end

      local.get $sock
      call $sock_close
      drop
      return
    end

    ;; Command: cat <file>
    local.get $cmd_ptr
    i32.const 860 ;; "cat"
    call $startswith
    if
      local.get $cmd_ptr
      i32.const 3
      i32.add
      local.set $arg_ptr
      (loop $loop_sc
        local.get $arg_ptr
        i32.load8_u
        i32.const 32
        i32.eq
        if
          local.get $arg_ptr
          i32.const 1
          i32.add
          local.set $arg_ptr
          br $loop_sc
        end
      )

      local.get $arg_ptr
      i32.load8_u
      i32.eqz
      if
        i32.const 3420
        call $term_puts
        call $term_newline
        return
      end

      ;; Open file via path_open
      i32.const 3
      i32.const 0
      local.get $arg_ptr
      local.get $arg_ptr
      call $strlen
      i32.const 0
      i64.const 64
      i64.const 0
      i32.const 0
      i32.const 15000 ;; fd_out
      call $path_open
      local.set $fd

      local.get $fd
      i32.const 0
      i32.ne
      if
        i32.const 3450
        call $term_puts
        local.get $arg_ptr
        call $term_puts
        call $term_newline
        return
      end

      ;; Setup iovec at 15010: buf=13000, len=1024
      i32.const 15010
      i32.const 13000
      i32.store

      i32.const 15014
      i32.const 1024
      i32.store

      ;; Read via fd_read
      i32.const 15000
      i32.load
      local.set $fd

      local.get $fd
      i32.const 15010
      i32.const 1
      i32.const 15020
      call $fd_read
      drop

      i32.const 15020
      i32.load
      local.set $read_bytes

      local.get $read_bytes
      i32.const 0
      i32.gt_s
      if
        i32.const 13000
        local.get $read_bytes
        i32.add
        i32.const 0
        i32.store8

        i32.const 13000
        call $term_puts
        call $term_newline
      end

      local.get $fd
      call $fd_close
      drop
      return
    end

    ;; Command: calc
    local.get $cmd_ptr
    i32.const 940 ;; "calc"
    call $streq
    if
      i32.const 710 ;; "Starting /CALC.WAS..."
      call $term_puts
      call $term_newline
      i32.const 780 ;; "/CALC.WAS\00"
      call $system_spawn
      drop
      return
    end

    ;; Command: notepad / edit
    local.get $cmd_ptr
    i32.const 960 ;; "notepad"
    call $streq
    local.get $cmd_ptr
    i32.const 970 ;; "edit"
    call $streq
    i32.or
    if
      i32.const 1000 ;; "Starting /NOTEPAD.WAS..."
      call $term_puts
      call $term_newline
      i32.const 980 ;; "/NOTEPAD.WAS\00"
      call $system_spawn
      drop
      return
    end

    ;; Command: explorer
    local.get $cmd_ptr
    i32.const 1030 ;; "explorer"
    call $streq
    if
      i32.const 1060 ;; "Starting /EXPLORER.WAS..."
      call $term_puts
      call $term_newline
      i32.const 1040 ;; "/EXPLORER.WAS\00"
      call $system_spawn
      drop
      return
    end

    ;; Command: exit
    local.get $cmd_ptr
    i32.const 950 ;; "exit"
    call $streq
    if
      i32.const 0
      call $proc_exit
      return
    end

    ;; Unknown command
    i32.const 740 ;; "woos: command not found: "
    call $term_puts
    local.get $cmd_ptr
    call $term_puts
    call $term_newline
  )

  ;; Main entry point
  (func $main (export "_start")
    (local $has_msg i32)
    (local $msg_type i32)
    (local $ascii i32)
    (local $scancode i32)

    ;; 1. Create Terminal Window (640x360)
    i32.const 640
    i32.const 360
    i32.const 500 ;; "Terminal\00"
    i32.const 0   ;; flags
    call $create_window_ex
    global.set $g_win_id

    ;; 2. Initialize screen buffer and print banner
    call $term_clear
    i32.const 540 ;; Banner
    call $term_puts
    call $term_newline
    i32.const 590 ;; Type 'help'...
    call $term_puts
    call $term_newline
    call $term_newline
    i32.const 520 ;; "woos:~$ "
    call $term_puts
    call $term_render

    ;; 3. Main event loop
    (loop $event_loop
      (block $break_events
        (loop $loop_events
          ;; Poll IPC message (resides at address 32)
          i32.const 32
          call $ipc_recv
          local.set $has_msg

          local.get $has_msg
          i32.eqz
          br_if $break_events

          ;; Load message fields
          i32.const 40
          i32.load
          local.set $msg_type

          i32.const 44
          i32.load
          local.set $ascii

          i32.const 48
          i32.load
          local.set $scancode

          ;; If MSG_CLOSE (type 3)
          local.get $msg_type
          i32.const 3
          i32.eq
          if
            i32.const 0
            call $proc_exit
          end

          ;; If MSG_KEY_DOWN (type 4)
          local.get $msg_type
          i32.const 4
          i32.eq
          if
            ;; Handle Enter (10 or 13)
            local.get $ascii
            i32.const 10
            i32.eq
            local.get $ascii
            i32.const 13
            i32.eq
            i32.or
            if
              ;; Null-terminate command buffer at 12000
              i32.const 12000
              global.get $g_input_len
              i32.add
              i32.const 0
              i32.store8

              call $term_exec_cmd

              i32.const 0
              global.set $g_input_len

              i32.const 520 ;; "woos:~$ "
              call $term_puts
              call $term_render
            else
              ;; Handle Backspace (8)
              local.get $ascii
              i32.const 8
              i32.eq
              if
                global.get $g_input_len
                i32.const 0
                i32.gt_s
                if
                  global.get $g_input_len
                  i32.const 1
                  i32.sub
                  global.set $g_input_len

                  i32.const 8 ;; Backspace
                  call $term_putc
                  call $term_render
                end
              else
                ;; Handle Printable ASCII [32..126]
                local.get $ascii
                i32.const 32
                i32.ge_s
                local.get $ascii
                i32.const 126
                i32.le_s
                i32.and
                if
                  global.get $g_input_len
                  i32.const 75
                  i32.lt_s
                  if
                    ;; Append to command buffer at 12000
                    i32.const 12000
                    global.get $g_input_len
                    i32.add
                    local.get $ascii
                    i32.store8

                    global.get $g_input_len
                    i32.const 1
                    i32.add
                    global.set $g_input_len

                    local.get $ascii
                    call $term_putc
                    call $term_render
                  end
                end
              end
            end
          end

          br $loop_events
        )
      )

      ;; Wait for next message (blocks thread until woken up by IPC)
      call $wait_message
      drop

      br $event_loop
    )
  )
)
