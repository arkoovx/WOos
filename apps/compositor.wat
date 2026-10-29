(module
  ;; Imports from env
  (import "env" "woos_graphics_get_info" (func $get_info (param i32) (result i32)))
  (import "env" "woos_graphics_present_rect" (func $present_rect (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "woos_input_poll_event" (func $poll_event (param i32) (result i32)))
  (import "env" "woos_debug_print" (func $debug_print (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "woos_input_wait_event" (func $wait_event (result i32)))
  (import "env" "woos_system_get_status" (func $get_status (param i32) (result i32)))

  ;; Imports for Windowing and IPC
  (import "env" "woos_graphics_set_window_pos" (func $set_window_pos (param i32 i32 i32) (result i32)))
  (import "env" "woos_graphics_get_window_count" (func $get_window_count (result i32)))
  (import "env" "woos_graphics_get_window_info" (func $get_window_info (param i32 i32) (result i32)))
  (import "env" "woos_graphics_draw_window_to_buffer" (func $draw_window_to_buffer (param i32 i32 i32 i32) (result i32)))
  (import "env" "woos_graphics_destroy_window" (func $destroy_window (param i32) (result i32)))
  (import "env" "woos_graphics_bring_to_front" (func $bring_to_front (param i32) (result i32)))
  (import "env" "woos_graphics_toggle_minimize" (func $toggle_minimize (param i32) (result i32)))
  (import "env" "woos_graphics_toggle_maximize" (func $toggle_maximize (param i32) (result i32)))
  (import "env" "woos_graphics_draw_text_to_buffer" (func $draw_text_raw (param i32 i32 i32 i32 i32 i32 i32) (result i32)))

  (import "env" "woos_ipc_send" (func $ipc_send (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "woos_ipc_recv" (func $ipc_recv (param i32) (result i32)))
  (import "env" "woos_system_spawn" (func $system_spawn (param i32) (result i32)))
  (import "env" "woos_graphics_get_focused_tid" (func $get_focused_tid (result i32)))

  ;; Import standard WASI functions
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))

  ;; Export linear memory: 90 pages (5.76 MB)
  (memory (export "memory") 90)

  ;; Data strings
  (data (i32.const 12000) "/CALC.WAS\00")
  (data (i32.const 12020) "Start\00")
  (data (i32.const 12030) "Applications\00")
  (data (i32.const 12050) "Calculator\00")
  (data (i32.const 12070) "SYS\00")
  (data (i32.const 12080) "RAM\00")
  (data (i32.const 12090) "HEAP\00")
  (data (i32.const 12100) "/TERM.WAS\00")
  (data (i32.const 12120) "Terminal\00")
  (data (i32.const 12140) "/NOTEPAD.WAS\00")
  (data (i32.const 12160) "Notepad\00")
  (data (i32.const 12180) "/EXPLORER.WAS\00")
  (data (i32.const 12200) "Explorer\00")

  ;; Globals
  (global $g_width (mut i32) (i32.const 1280))
  (global $g_height (mut i32) (i32.const 1024))
  (global $g_pitch (mut i32) (i32.const 5120))
  (global $g_bpp (mut i32) (i32.const 32))

  ;; Cursor coordinates
  (global $g_cursor_x (mut i32) (i32.const 640))
  (global $g_cursor_y (mut i32) (i32.const 512))
  (global $g_old_x (mut i32) (i32.const 640))
  (global $g_old_y (mut i32) (i32.const 512))

  ;; Drag and Drop state
  (global $g_drag_win_id (mut i32) (i32.const -1))
  (global $g_drag_off_x (mut i32) (i32.const 0))
  (global $g_drag_off_y (mut i32) (i32.const 0))
  (global $g_drag_win_w (mut i32) (i32.const 240))
  (global $g_drag_win_h (mut i32) (i32.const 320))

  ;; Start Menu state
  (global $g_start_open (mut i32) (i32.const 0))

  ;; Saved status metrics to prevent redundant rendering
  (global $g_last_uptime (mut i32) (i32.const -1))
  (global $g_last_free_mem (mut i32) (i32.const -1))
  (global $g_last_heap_used (mut i32) (i32.const -1))
  (global $g_last_net_link (mut i32) (i32.const -1))

  ;; Clipping bounds for optimized drawing
  (global $g_clip_min_x (mut i32) (i32.const 0))
  (global $g_clip_max_x (mut i32) (i32.const 1280))
  (global $g_clip_min_y (mut i32) (i32.const 0))
  (global $g_clip_max_y (mut i32) (i32.const 1024))

  ;; Draw solid rectangle onto buffer with clipping
  (func $draw_rect (param $x i32) (param $y i32) (param $w i32) (param $h i32) (param $color i32)
    (local $py i32)
    (local $px i32)
    (local $row_offset i32)
    (local $pixel_ptr i32)
    (local $x_end i32)
    (local $y_end i32)

    local.get $x
    local.get $w
    i32.add
    local.set $x_end

    local.get $y
    local.get $h
    i32.add
    local.set $y_end

    local.get $x
    global.get $g_clip_min_x
    i32.lt_s
    if
      global.get $g_clip_min_x
      local.set $x
    end

    local.get $x_end
    global.get $g_clip_max_x
    i32.gt_s
    if
      global.get $g_clip_max_x
      local.set $x_end
    end

    local.get $y
    global.get $g_clip_min_y
    i32.lt_s
    if
      global.get $g_clip_min_y
      local.set $y
    end

    local.get $y_end
    global.get $g_clip_max_y
    i32.gt_s
    if
      global.get $g_clip_max_y
      local.set $y_end
    end

    local.get $x
    local.get $x_end
    i32.ge_s
    if
      return
    end

    local.get $y
    local.get $y_end
    i32.ge_s
    if
      return
    end

    local.get $y
    local.set $py
    (block $break_y
      (loop $loop_y
        local.get $py
        local.get $y_end
        i32.ge_s
        br_if $break_y

        local.get $py
        global.get $g_pitch
        i32.mul
        local.set $row_offset

        local.get $x
        local.set $px
        (block $break_x
          (loop $loop_x
            local.get $px
            local.get $x_end
            i32.ge_s
            br_if $break_x

            i32.const 65536
            local.get $row_offset
            i32.add
            local.get $px
            i32.const 4
            i32.mul
            i32.add
            local.set $pixel_ptr

            local.get $pixel_ptr
            local.get $color
            i32.store

            local.get $px
            i32.const 1
            i32.add
            local.set $px
            br $loop_x
          )
        )

        local.get $py
        i32.const 1
        i32.add
        local.set $py
        br $loop_y
      )
    )
  )

  ;; Draw null-terminated string onto linear memory backbuffer with clipping checks
  (func $draw_str (param $x i32) (param $y i32) (param $str_ptr i32) (param $color i32)
    local.get $y
    i32.const 16
    i32.add
    global.get $g_clip_min_y
    i32.lt_s
    if
      return
    end

    local.get $y
    global.get $g_clip_max_y
    i32.gt_s
    if
      return
    end

    local.get $x
    i32.const 160
    i32.add
    global.get $g_clip_min_x
    i32.lt_s
    if
      return
    end

    local.get $x
    global.get $g_clip_max_x
    i32.gt_s
    if
      return
    end

    i32.const 65536
    global.get $g_width
    global.get $g_height
    local.get $x
    local.get $y
    local.get $str_ptr
    local.get $color
    call $draw_text_raw
    drop
  )

  ;; Compare two null-terminated strings: returns 1 if equal, 0 if not
  (func $streq (param $s1 i32) (param $s2 i32) (result i32)
    (local $c1 i32)
    (local $c2 i32)
    (loop $loop
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
      br $loop
    )
    i32.const 0
  )

  ;; Unified single-instance launcher: if window with $title exists, brings to front & restores; else spawns $path
  (func $launch_or_focus (param $path i32) (param $title i32)
    (local $win_count i32)
    (local $idx i32)
    (local $found_id i32)

    i32.const -1
    local.set $found_id

    call $get_window_count
    local.set $win_count

    i32.const 0
    local.set $idx
    (block $break_search
      (loop $loop_search
        local.get $idx
        local.get $win_count
        i32.ge_s
        br_if $break_search

        local.get $idx
        i32.const 10000
        call $get_window_info
        drop

        ;; Compare window title (offset 44) with param $title
        i32.const 10044
        local.get $title
        call $streq
        if
          i32.const 10000
          i32.load ;; window id
          local.set $found_id
          br $break_search
        end

        local.get $idx
        i32.const 1
        i32.add
        local.set $idx
        br $loop_search
      )
    )

    local.get $found_id
    i32.const -1
    i32.ne
    if
      ;; Window already exists: bring to front & restore!
      local.get $found_id
      call $bring_to_front
      drop
    else
      ;; Window does not exist: spawn application!
      local.get $path
      call $system_spawn
      drop
    end
  )

  ;; Renders mouse cursor (handled in present_rect)
  (func $draw_cursor (param $cx i32) (param $cy i32) (param $color i32))

  ;; Cursor underlay stubs (cursor overlay is handled cleanly in present_rect)
  (func $save_cursor_underlay (param $cx i32) (param $cy i32))
  (func $restore_cursor_underlay (param $cx i32) (param $cy i32))

  ;; Draw Close Button [X]
  (func $draw_btn_close (param $bx i32) (param $by i32) (param $bw i32) (param $bh i32)
    (local $cx i32)
    (local $cy i32)

    local.get $bx
    local.get $by
    local.get $bw
    local.get $bh
    i32.const 0xC0392B
    call $draw_rect

    local.get $bx
    local.get $bw
    i32.const 1
    i32.shr_u
    i32.add
    local.set $cx

    local.get $by
    local.get $bh
    i32.const 1
    i32.shr_u
    i32.add
    local.set $cy

    local.get $cx
    i32.const 4
    i32.sub
    local.get $cy
    i32.const 4
    i32.sub
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 2
    i32.sub
    local.get $cy
    i32.const 2
    i32.sub
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 1
    i32.sub
    local.get $cy
    i32.const 1
    i32.sub
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 1
    i32.add
    local.get $cy
    i32.const 1
    i32.add
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 3
    i32.add
    local.get $cy
    i32.const 3
    i32.add
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 4
    i32.sub
    local.get $cy
    i32.const 3
    i32.add
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 2
    i32.sub
    local.get $cy
    i32.const 1
    i32.add
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 1
    i32.add
    local.get $cy
    i32.const 2
    i32.sub
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect

    local.get $cx
    i32.const 3
    i32.add
    local.get $cy
    i32.const 4
    i32.sub
    i32.const 2
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect
  )

  ;; Draw Maximize/Restore Button [□]
  (func $draw_btn_maximize (param $bx i32) (param $by i32) (param $bw i32) (param $bh i32) (param $is_max i32) (param $bg_col i32)
    (local $cx i32)
    (local $cy i32)

    local.get $bx
    local.get $by
    local.get $bw
    local.get $bh
    local.get $bg_col
    call $draw_rect

    local.get $bx
    local.get $bw
    i32.const 1
    i32.shr_u
    i32.add
    local.set $cx

    local.get $by
    local.get $bh
    i32.const 1
    i32.shr_u
    i32.add
    local.set $cy

    local.get $is_max
    if
      local.get $cx
      i32.const 2
      i32.sub
      local.get $cy
      i32.const 4
      i32.sub
      i32.const 7
      i32.const 2
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 3
      i32.add
      local.get $cy
      i32.const 4
      i32.sub
      i32.const 2
      i32.const 5
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 4
      i32.sub
      local.get $cy
      i32.const 2
      i32.sub
      i32.const 7
      i32.const 2
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 4
      i32.sub
      local.get $cy
      i32.const 3
      i32.add
      i32.const 7
      i32.const 1
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 4
      i32.sub
      local.get $cy
      i32.const 2
      i32.sub
      i32.const 1
      i32.const 6
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 2
      i32.add
      local.get $cy
      i32.const 2
      i32.sub
      i32.const 1
      i32.const 6
      i32.const 0xFFFFFF
      call $draw_rect
    else
      local.get $cx
      i32.const 4
      i32.sub
      local.get $cy
      i32.const 4
      i32.sub
      i32.const 9
      i32.const 2
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 4
      i32.sub
      local.get $cy
      i32.const 3
      i32.add
      i32.const 9
      i32.const 1
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 4
      i32.sub
      local.get $cy
      i32.const 4
      i32.sub
      i32.const 1
      i32.const 8
      i32.const 0xFFFFFF
      call $draw_rect

      local.get $cx
      i32.const 4
      i32.add
      local.get $cy
      i32.const 4
      i32.sub
      i32.const 1
      i32.const 8
      i32.const 0xFFFFFF
      call $draw_rect
    end
  )

  ;; Draw Minimize Button [_]
  (func $draw_btn_minimize (param $bx i32) (param $by i32) (param $bw i32) (param $bh i32) (param $bg_col i32)
    (local $cx i32)
    (local $cy i32)

    local.get $bx
    local.get $by
    local.get $bw
    local.get $bh
    local.get $bg_col
    call $draw_rect

    local.get $bx
    local.get $bw
    i32.const 1
    i32.shr_u
    i32.add
    local.set $cx

    local.get $by
    local.get $bh
    i32.const 1
    i32.shr_u
    i32.add
    local.set $cy

    local.get $cx
    i32.const 4
    i32.sub
    local.get $cy
    i32.const 3
    i32.add
    i32.const 9
    i32.const 2
    i32.const 0xFFFFFF
    call $draw_rect
  )

  ;; Draw Taskbar & Indicators & Start Menu
  (func $draw_taskbar
    (local $tb_y i32)
    (local $win_count i32)
    (local $idx i32)
    (local $win_id i32)
    (local $win_flags i32)
    (local $tab_x i32)
    (local $tab_y i32)
    (local $is_focused i32)
    (local $is_min i32)
    (local $tab_bg i32)
    (local $text_col i32)

    (local $uptime_ms i32)
    (local $total_pages i32)
    (local $free_pages i32)
    (local $used_pages i32)
    (local $ram_bar_w i32)
    (local $heap_used i64)
    (local $heap_free i64)
    (local $heap_bar_w i32)
    (local $net_link_up i32)
    (local $heartbeat_color i32)

    global.get $g_height
    i32.const 36
    i32.sub
    local.set $tb_y

    ;; 1. Taskbar Background
    i32.const 0
    local.get $tb_y
    global.get $g_width
    i32.const 36
    i32.const 0x1F242D
    call $draw_rect

    ;; Top 1px slate divider
    i32.const 0
    local.get $tb_y
    global.get $g_width
    i32.const 1
    i32.const 0x3E4F63
    call $draw_rect

    ;; 2. Start Button
    i32.const 4
    local.get $tb_y
    i32.const 4
    i32.add
    i32.const 76
    i32.const 28
    global.get $g_start_open
    if (result i32)
      i32.const 0x106EBE
    else
      i32.const 0x0078D7
    end
    call $draw_rect

    ;; Start Logo (4 white boxes)
    i32.const 10
    local.get $tb_y
    i32.const 10
    i32.add
    i32.const 5
    i32.const 5
    i32.const 0xFFFFFF
    call $draw_rect

    i32.const 17
    local.get $tb_y
    i32.const 10
    i32.add
    i32.const 5
    i32.const 5
    i32.const 0xFFFFFF
    call $draw_rect

    i32.const 10
    local.get $tb_y
    i32.const 17
    i32.add
    i32.const 5
    i32.const 5
    i32.const 0xFFFFFF
    call $draw_rect

    i32.const 17
    local.get $tb_y
    i32.const 17
    i32.add
    i32.const 5
    i32.const 5
    i32.const 0xFFFFFF
    call $draw_rect

    ;; Text "Start"
    i32.const 27
    local.get $tb_y
    i32.const 10
    i32.add
    i32.const 12020 ;; "Start\00"
    i32.const 0xFFFFFF
    call $draw_str

    ;; 3. Dynamic Application Tabs
    call $get_window_count
    local.set $win_count

    i32.const 0
    local.set $idx
    (block $break_tabs
      (loop $loop_tabs
        local.get $idx
        local.get $win_count
        i32.ge_s
        br_if $break_tabs

        local.get $idx
        i32.const 10000
        call $get_window_info
        drop

        i32.const 10000
        i32.load
        local.set $win_id

        i32.const 10000
        i32.load offset=24
        local.set $win_flags

        ;; tab_x = 86 + idx * 145
        i32.const 86
        local.get $idx
        i32.const 145
        i32.mul
        i32.add
        local.set $tab_x

        local.get $tb_y
        i32.const 4
        i32.add
        local.set $tab_y

        local.get $win_flags
        i32.const 8 ;; WOOS_WIN_FLAG_FOCUSED
        i32.and
        local.set $is_focused

        local.get $win_flags
        i32.const 1 ;; WOOS_WIN_FLAG_MINIMIZED
        i32.and
        local.set $is_min

        local.get $is_focused
        if (result i32)
          i32.const 0x2E3A48
        else
          local.get $is_min
          if (result i32)
            i32.const 0x181C22
          else
            i32.const 0x252C36
          end
        end
        local.set $tab_bg

        ;; Draw tab rectangle
        local.get $tab_x
        local.get $tab_y
        i32.const 140
        i32.const 28
        local.get $tab_bg
        call $draw_rect

        ;; Tab border 1px
        local.get $tab_x
        local.get $tab_y
        i32.const 140
        i32.const 1
        i32.const 0x3D4958
        call $draw_rect

        local.get $tab_x
        local.get $tab_y
        i32.const 27
        i32.add
        i32.const 140
        i32.const 1
        i32.const 0x3D4958
        call $draw_rect

        local.get $tab_x
        local.get $tab_y
        i32.const 1
        i32.const 28
        i32.const 0x3D4958
        call $draw_rect

        local.get $tab_x
        i32.const 139
        i32.add
        local.get $tab_y
        i32.const 1
        i32.const 28
        i32.const 0x3D4958
        call $draw_rect

        ;; Active accent bottom underline
        local.get $is_focused
        if
          local.get $tab_x
          local.get $tab_y
          i32.const 26
          i32.add
          i32.const 140
          i32.const 2
          i32.const 0x0078D7
          call $draw_rect
        end

        ;; Tab mini icon
        local.get $tab_x
        i32.const 7
        i32.add
        local.get $tab_y
        i32.const 7
        i32.add
        i32.const 12
        i32.const 12
        i32.const 0xD35400
        call $draw_rect

        local.get $tab_x
        i32.const 9
        i32.add
        local.get $tab_y
        i32.const 9
        i32.add
        i32.const 8
        i32.const 8
        i32.const 0xECF0F1
        call $draw_rect

        ;; Tab title string at 10044
        local.get $is_focused
        if (result i32)
          i32.const 0xFFFFFF
        else
          local.get $is_min
          if (result i32)
            i32.const 0x7F8C8D
          else
            i32.const 0xBDC3C7
          end
        end
        local.set $text_col

        local.get $tab_x
        i32.const 23
        i32.add
        local.get $tab_y
        i32.const 10
        i32.add
        i32.const 10044
        local.get $text_col
        call $draw_str

        local.get $idx
        i32.const 1
        i32.add
        local.set $idx
        br $loop_tabs
      )
    )

    ;; 4. System Tray (Right Side)
    global.get $g_width
    i32.const 295
    i32.sub
    local.get $tb_y
    i32.const 6
    i32.add
    i32.const 1
    i32.const 24
    i32.const 0x3E4F63
    call $draw_rect

    ;; Poll system status into address 1000
    i32.const 1000
    call $get_status
    drop

    i32.const 1000
    i32.load
    local.set $uptime_ms

    i32.const 1004
    i32.load
    local.set $total_pages

    i32.const 1008
    i32.load
    local.set $free_pages

    i32.const 1012
    i64.load
    local.set $heap_used

    i32.const 1020
    i64.load
    local.set $heap_free

    i32.const 1039
    i32.load8_u
    local.set $net_link_up

    ;; Heartbeat LED
    local.get $uptime_ms
    i32.const 500
    i32.div_u
    i32.const 2
    i32.rem_u
    if (result i32)
      i32.const 0x2ECC71
    else
      i32.const 0x27AE60
    end
    local.set $heartbeat_color

    global.get $g_width
    i32.const 285
    i32.sub
    local.get $tb_y
    i32.const 12
    i32.add
    i32.const 12
    i32.const 12
    local.get $heartbeat_color
    call $draw_rect

    ;; Network LED
    global.get $g_width
    i32.const 265
    i32.sub
    local.get $tb_y
    i32.const 12
    i32.add
    i32.const 12
    i32.const 12
    local.get $net_link_up
    if (result i32)
      i32.const 0x2ECC71
    else
      i32.const 0xE74C3C
    end
    call $draw_rect

    ;; RAM label & bar
    global.get $g_width
    i32.const 242
    i32.sub
    local.get $tb_y
    i32.const 10
    i32.add
    i32.const 12080 ;; "RAM\00"
    i32.const 0xA0AAB5
    call $draw_str

    global.get $g_width
    i32.const 210
    i32.sub
    local.get $tb_y
    i32.const 11
    i32.add
    i32.const 70
    i32.const 14
    i32.const 0x505763
    call $draw_rect

    local.get $total_pages
    local.get $free_pages
    i32.sub
    local.set $used_pages

    local.get $total_pages
    i32.const 0
    i32.gt_s
    if
      local.get $used_pages
      i32.const 70
      i32.mul
      local.get $total_pages
      i32.div_u
      local.set $ram_bar_w
    else
      i32.const 0
      local.set $ram_bar_w
    end

    local.get $ram_bar_w
    i32.const 0
    i32.gt_s
    if
      global.get $g_width
      i32.const 210
      i32.sub
      local.get $tb_y
      i32.const 11
      i32.add
      local.get $ram_bar_w
      i32.const 14
      i32.const 0x9B59B6
      call $draw_rect
    end

    ;; HEAP label & bar
    global.get $g_width
    i32.const 130
    i32.sub
    local.get $tb_y
    i32.const 10
    i32.add
    i32.const 12090 ;; "HEAP\00"
    i32.const 0xA0AAB5
    call $draw_str

    global.get $g_width
    i32.const 90
    i32.sub
    local.get $tb_y
    i32.const 11
    i32.add
    i32.const 80
    i32.const 14
    i32.const 0x505763
    call $draw_rect

    local.get $heap_used
    local.get $heap_free
    i64.add
    local.set $heap_free

    local.get $heap_free
    i64.const 0
    i64.gt_s
    if
      local.get $heap_used
      i64.const 80
      i64.mul
      local.get $heap_free
      i64.div_u
      i32.wrap_i64
      local.set $heap_bar_w
    else
      i32.const 0
      local.set $heap_bar_w
    end

    local.get $heap_bar_w
    i32.const 0
    i32.gt_s
    if
      global.get $g_width
      i32.const 90
      i32.sub
      local.get $tb_y
      i32.const 11
      i32.add
      local.get $heap_bar_w
      i32.const 14
      i32.const 0x3498DB
      call $draw_rect
    end

    ;; 5. Start Menu Popup (if open)
    global.get $g_start_open
    if
      i32.const 4
      local.get $tb_y
      i32.const 160
      i32.sub
      i32.const 200
      i32.const 160
      i32.const 0x20252D
      call $draw_rect

      i32.const 4
      local.get $tb_y
      i32.const 160
      i32.sub
      i32.const 200
      i32.const 1
      i32.const 0x3E4F63
      call $draw_rect

      i32.const 4
      local.get $tb_y
      i32.const 160
      i32.sub
      i32.const 1
      i32.const 160
      i32.const 0x3E4F63
      call $draw_rect

      i32.const 203
      local.get $tb_y
      i32.const 160
      i32.sub
      i32.const 1
      i32.const 160
      i32.const 0x3E4F63
      call $draw_rect

      i32.const 4
      local.get $tb_y
      i32.const 160
      i32.sub
      i32.const 200
      i32.const 28
      i32.const 0x181C22
      call $draw_rect

      i32.const 14
      local.get $tb_y
      i32.const 150
      i32.sub
      i32.const 12030 ;; "Applications\00"
      i32.const 0x0078D7
      call $draw_str

      ;; Item 1: Calculator
      i32.const 12
      local.get $tb_y
      i32.const 124
      i32.sub
      i32.const 184
      i32.const 32
      i32.const 0x28303B
      call $draw_rect

      i32.const 18
      local.get $tb_y
      i32.const 118
      i32.sub
      i32.const 20
      i32.const 20
      i32.const 0xD35400
      call $draw_rect

      i32.const 22
      local.get $tb_y
      i32.const 122
      i32.sub
      i32.const 12
      i32.const 12
      i32.const 0xECF0F1
      call $draw_rect

      i32.const 46
      local.get $tb_y
      i32.const 112
      i32.sub
      i32.const 12050 ;; "Calculator\00"
      i32.const 0xFFFFFF
      call $draw_str

      ;; Item 2: Terminal
      i32.const 12
      local.get $tb_y
      i32.const 88
      i32.sub
      i32.const 184
      i32.const 32
      i32.const 0x28303B
      call $draw_rect

      i32.const 18
      local.get $tb_y
      i32.const 82
      i32.sub
      i32.const 20
      i32.const 20
      i32.const 0x1E1E1E
      call $draw_rect

      i32.const 20
      local.get $tb_y
      i32.const 80
      i32.sub
      i32.const 16
      i32.const 16
      i32.const 0x0C0C0C
      call $draw_rect

      i32.const 23
      local.get $tb_y
      i32.const 73
      i32.sub
      i32.const 4
      i32.const 2
      i32.const 0x2ECC71
      call $draw_rect

      i32.const 29
      local.get $tb_y
      i32.const 70
      i32.sub
      i32.const 5
      i32.const 2
      i32.const 0x2ECC71
      call $draw_rect

      i32.const 46
      local.get $tb_y
      i32.const 76
      i32.sub
      i32.const 12120 ;; "Terminal\00"
      i32.const 0xFFFFFF
      call $draw_str
    end
  )

  ;; Renders wallpaper, desktop icons, windows in z-order, taskbar, cursor
  (func $redraw_all
    (local $win_count i32)
    (local $idx i32)
    (local $win_id i32)
    (local $win_x i32)
    (local $win_y i32)
    (local $win_w i32)
    (local $win_h i32)
    (local $win_flags i32)
    (local $is_focused i32)
    (local $frame_col i32)
    (local $title_col i32)
    (local $text_col i32)

    ;; 1. Desktop Wallpaper Background (clipped to dirty rect)
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    i32.const 0x1A222D
    call $draw_rect

    ;; 2. Desktop Shortcut: Calculator (at 50, 50, 64x64)
    i32.const 50
    i32.const 50
    i32.const 64
    i32.const 64
    i32.const 0xD35400
    call $draw_rect

    i32.const 58
    i32.const 58
    i32.const 48
    i32.const 12
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 58
    i32.const 76
    i32.const 10
    i32.const 10
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 72
    i32.const 76
    i32.const 10
    i32.const 10
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 86
    i32.const 76
    i32.const 10
    i32.const 10
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 58
    i32.const 90
    i32.const 10
    i32.const 10
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 72
    i32.const 90
    i32.const 10
    i32.const 10
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 86
    i32.const 90
    i32.const 10
    i32.const 10
    i32.const 0xECF0F1
    call $draw_rect

    i32.const 42
    i32.const 120
    i32.const 12050 ;; "Calculator\00"
    i32.const 0xFFFFFF
    call $draw_str

    ;; Desktop Shortcut 2: Terminal (at 150, 50, 64x64)
    i32.const 150
    i32.const 50
    i32.const 64
    i32.const 64
    i32.const 0x1E1E1E
    call $draw_rect

    i32.const 154
    i32.const 54
    i32.const 56
    i32.const 56
    i32.const 0x0C0C0C
    call $draw_rect

    ;; Prompt >_ in green (0x2ECC71)
    i32.const 162
    i32.const 70
    i32.const 8
    i32.const 4
    i32.const 0x2ECC71
    call $draw_rect

    i32.const 166
    i32.const 74
    i32.const 8
    i32.const 4
    i32.const 0x2ECC71
    call $draw_rect

    i32.const 162
    i32.const 78
    i32.const 8
    i32.const 4
    i32.const 0x2ECC71
    call $draw_rect

    i32.const 178
    i32.const 82
    i32.const 16
    i32.const 4
    i32.const 0x2ECC71
    call $draw_rect

    i32.const 152
    i32.const 120
    i32.const 12120 ;; "Terminal\00"
    i32.const 0xFFFFFF
    call $draw_str

    ;; Desktop Shortcut 3: Notepad (at 250, 50, 64x64)
    i32.const 250
    i32.const 50
    i32.const 64
    i32.const 64
    i32.const 0x2980B9
    call $draw_rect

    i32.const 254
    i32.const 54
    i32.const 56
    i32.const 56
    i32.const 0xFFFFFF
    call $draw_rect

    ;; Notepad lines inside icon
    i32.const 262
    i32.const 64
    i32.const 40
    i32.const 4
    i32.const 0x34495E
    call $draw_rect

    i32.const 262
    i32.const 74
    i32.const 40
    i32.const 4
    i32.const 0x34495E
    call $draw_rect

    i32.const 262
    i32.const 84
    i32.const 28
    i32.const 4
    i32.const 0x34495E
    call $draw_rect

    i32.const 254
    i32.const 120
    i32.const 12160 ;; "Notepad\00"
    i32.const 0xFFFFFF
    call $draw_str

    ;; Desktop Shortcut 4: Explorer (at 350, 50, 64x64)
    i32.const 350
    i32.const 50
    i32.const 64
    i32.const 64
    i32.const 0xD4AC0D
    call $draw_rect

    i32.const 354
    i32.const 54
    i32.const 56
    i32.const 56
    i32.const 0xF4D03F
    call $draw_rect

    ;; Folder front flap
    i32.const 354
    i32.const 70
    i32.const 56
    i32.const 40
    i32.const 0xF39C12
    call $draw_rect

    ;; Folder border
    i32.const 354
    i32.const 70
    i32.const 56
    i32.const 2
    i32.const 0xB7950B
    call $draw_rect

    i32.const 352
    i32.const 120
    i32.const 12200 ;; "Explorer\00"
    i32.const 0xFFFFFF
    call $draw_str

    ;; 3. Iterate and Draw Windows in z-order
    call $get_window_count
    local.set $win_count

    i32.const 0
    local.set $idx
    (block $break_win
      (loop $loop_win
        local.get $idx
        local.get $win_count
        i32.ge_s
        br_if $break_win

        local.get $idx
        i32.const 10000
        call $get_window_info
        drop

        i32.const 10000
        i32.load
        local.set $win_id

        i32.const 10000
        i32.load offset=8
        local.set $win_x

        i32.const 10000
        i32.load offset=12
        local.set $win_y

        i32.const 10000
        i32.load offset=16
        local.set $win_w

        i32.const 10000
        i32.load offset=20
        local.set $win_h

        i32.const 10000
        i32.load offset=24
        local.set $win_flags

        ;; If window is minimized, skip desktop drawing
        local.get $win_flags
        i32.const 1 ;; WOOS_WIN_FLAG_MINIMIZED
        i32.and
        if
          local.get $idx
          i32.const 1
          i32.add
          local.set $idx
          br $loop_win
        end

        ;; Check Custom Decor
        local.get $win_flags
        i32.const 4 ;; WOOS_WIN_FLAG_CUSTOM_DECOR
        i32.and
        if
          local.get $win_id
          i32.const 65536
          global.get $g_width
          global.get $g_height
          call $draw_window_to_buffer
          drop
        else
          ;; Standard Windows Frame
          local.get $win_flags
          i32.const 8 ;; WOOS_WIN_FLAG_FOCUSED
          i32.and
          local.set $is_focused

          local.get $is_focused
          if (result i32)
            i32.const 0x0078D7
          else
            i32.const 0x4B535D
          end
          local.set $frame_col

          local.get $is_focused
          if (result i32)
            i32.const 0x0078D7
          else
            i32.const 0x333A42
          end
          local.set $title_col

          local.get $is_focused
          if (result i32)
            i32.const 0xFFFFFF
          else
            i32.const 0xBDC3C7
          end
          local.set $text_col

          ;; 3px Outer Frame Border
          local.get $win_x
          i32.const 3
          i32.sub
          local.get $win_y
          i32.const 25
          i32.sub
          local.get $win_w
          i32.const 6
          i32.add
          local.get $win_h
          i32.const 28
          i32.add
          local.get $frame_col
          call $draw_rect

          ;; Titlebar
          local.get $win_x
          local.get $win_y
          i32.const 22
          i32.sub
          local.get $win_w
          i32.const 22
          local.get $title_col
          call $draw_rect

          ;; Mini Window Icon
          local.get $win_x
          i32.const 5
          i32.add
          local.get $win_y
          i32.const 17
          i32.sub
          i32.const 10
          i32.const 10
          i32.const 0x3498DB
          call $draw_rect

          local.get $win_x
          i32.const 7
          i32.add
          local.get $win_y
          i32.const 15
          i32.sub
          i32.const 6
          i32.const 6
          i32.const 0xFFFFFF
          call $draw_rect

          ;; Window Title String at 10044
          local.get $win_x
          i32.const 19
          i32.add
          local.get $win_y
          i32.const 15
          i32.sub
          i32.const 10044
          local.get $text_col
          call $draw_str

          ;; Caption Button [X] Close
          local.get $win_x
          local.get $win_w
          i32.add
          i32.const 24
          i32.sub
          local.get $win_y
          i32.const 21
          i32.sub
          i32.const 23
          i32.const 19
          call $draw_btn_close

          ;; Caption Button [□] Maximize/Restore
          local.get $win_x
          local.get $win_w
          i32.add
          i32.const 48
          i32.sub
          local.get $win_y
          i32.const 21
          i32.sub
          i32.const 23
          i32.const 19
          local.get $win_flags
          i32.const 2 ;; WOOS_WIN_FLAG_MAXIMIZED
          i32.and
          local.get $title_col
          call $draw_btn_maximize

          ;; Caption Button [_] Minimize
          local.get $win_x
          local.get $win_w
          i32.add
          i32.const 72
          i32.sub
          local.get $win_y
          i32.const 21
          i32.sub
          i32.const 23
          i32.const 19
          local.get $title_col
          call $draw_btn_minimize

          ;; Window Client Body
          local.get $win_id
          i32.const 65536
          global.get $g_width
          global.get $g_height
          call $draw_window_to_buffer
          drop
        end

        local.get $idx
        i32.const 1
        i32.add
        local.set $idx
        br $loop_win
      )
    )

    ;; 4. Draw Taskbar only if dirty rect intersects taskbar area
    global.get $g_clip_max_y
    global.get $g_height
    i32.const 36
    i32.sub
    i32.ge_s
    if
      call $draw_taskbar
    end
  )

  ;; Entry point _start
  (func (export "_start")
    (local $event_popped i32)
    (local $ev_type i32)
    (local $ev_x i32)
    (local $ev_y i32)
    (local $ev_btn i32)

    (local $win_count i32)
    (local $idx i32)
    (local $win_id i32)
    (local $win_tid i32)
    (local $win_x i32)
    (local $win_y i32)
    (local $win_w i32)
    (local $win_h i32)
    (local $win_flags i32)
    (local $tab_x i32)

    (local $has_moved i32)
    (local $min_x i32)
    (local $max_x i32)
    (local $min_y i32)
    (local $max_y i32)
    (local $temp i32)
    (local $need_redraw i32)
    (local $force_full_redraw i32)

    ;; 1. Get Video parameters
    i32.const 0
    call $get_info
    drop

    i32.const 0
    i32.load
    global.set $g_width

    i32.const 4
    i32.load
    global.set $g_height

    i32.const 8
    i32.load
    global.set $g_pitch

    i32.const 12
    i32.load
    global.set $g_bpp

    ;; Initial UI render
    call $redraw_all
    i32.const 65536
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    call $present_rect
    drop

    ;; 2. Main event loop
    (loop $event_loop
      i32.const 0
      local.set $need_redraw
      i32.const 0
      local.set $force_full_redraw
      i32.const 0
      local.set $has_moved
      i32.const 9999
      local.set $min_x
      i32.const -9999
      local.set $max_x
      i32.const 9999
      local.set $min_y
      i32.const -9999
      local.set $max_y

      ;; Process all pending events in a non-blocking loop
      (block $break_events
        (loop $loop_events
          ;; Poll next input event
          i32.const 32
          call $poll_event
          local.set $event_popped

          local.get $event_popped
          i32.eqz
          br_if $break_events

          ;; Load event fields
          i32.const 32
          i32.load
          local.set $ev_type

          i32.const 36
          i32.load16_u
          local.set $ev_x

          i32.const 38
          i32.load16_u
          local.set $ev_y

          i32.const 40
          i32.load8_u
          local.set $ev_btn

          ;; Handle Mouse Move (type == 0)
          local.get $ev_type
          i32.const 0
          i32.eq
          if
            ;; Update coordinates
            global.get $g_cursor_x
            global.set $g_old_x
            global.get $g_cursor_y
            global.set $g_old_y

            local.get $ev_x
            global.set $g_cursor_x
            local.get $ev_y
            global.set $g_cursor_y

            ;; If we are dragging a window, update its position & dirty bounds
            global.get $g_drag_win_id
            i32.const -1
            i32.ne
            if
              ;; Compute new window Y with minimum boundary clamp (25px)
              global.get $g_cursor_y
              global.get $g_drag_off_y
              i32.sub
              local.set $temp

              local.get $temp
              i32.const 25
              i32.lt_s
              if
                i32.const 25
                local.set $temp
              end

              global.get $g_drag_win_id
              global.get $g_cursor_x
              global.get $g_drag_off_x
              i32.sub
              local.get $temp
              call $set_window_pos
              drop

              ;; Expand dirty box to cover BOTH old window and new window:
              ;; 1. min_x = min(g_old_x, g_cursor_x) - g_drag_off_x - 3
              global.get $g_old_x
              global.get $g_cursor_x
              i32.lt_s
              if (result i32)
                global.get $g_old_x
              else
                global.get $g_cursor_x
              end
              global.get $g_drag_off_x
              i32.sub
              i32.const 3
              i32.sub
              local.set $temp

              local.get $temp
              local.get $min_x
              i32.lt_s
              if
                local.get $temp
                local.set $min_x
              end

              ;; 2. max_x = max(g_old_x, g_cursor_x) - g_drag_off_x + win_w + 3
              global.get $g_old_x
              global.get $g_cursor_x
              i32.gt_s
              if (result i32)
                global.get $g_old_x
              else
                global.get $g_cursor_x
              end
              global.get $g_drag_off_x
              i32.sub
              global.get $g_drag_win_w
              i32.add
              i32.const 3
              i32.add
              local.set $temp

              local.get $temp
              local.get $max_x
              i32.gt_s
              if
                local.get $temp
                local.set $max_x
              end

              ;; 3. min_y = min(g_old_y, g_cursor_y) - g_drag_off_y - 25
              global.get $g_old_y
              global.get $g_cursor_y
              i32.lt_s
              if (result i32)
                global.get $g_old_y
              else
                global.get $g_cursor_y
              end
              global.get $g_drag_off_y
              i32.sub
              i32.const 25
              i32.sub
              local.set $temp

              local.get $temp
              local.get $min_y
              i32.lt_s
              if
                local.get $temp
                local.set $min_y
              end

              ;; 4. max_y = max(g_old_y, g_cursor_y) - g_drag_off_y + win_h + 3
              global.get $g_old_y
              global.get $g_cursor_y
              i32.gt_s
              if (result i32)
                global.get $g_old_y
              else
                global.get $g_cursor_y
              end
              global.get $g_drag_off_y
              i32.sub
              global.get $g_drag_win_h
              i32.add
              i32.const 3
              i32.add
              local.set $temp

              local.get $temp
              local.get $max_y
              i32.gt_s
              if
                local.get $temp
                local.set $max_y
              end

              i32.const 1
              local.set $need_redraw
            end

            ;; Mark that mouse moved
            i32.const 1
            local.set $has_moved

            ;; Include cursor in min_x / max_x / min_y / max_y:
            global.get $g_old_x
            global.get $g_cursor_x
            i32.lt_s
            if (result i32)
              global.get $g_old_x
            else
              global.get $g_cursor_x
            end
            i32.const 6
            i32.sub
            local.set $temp

            local.get $temp
            local.get $min_x
            i32.lt_s
            if
              local.get $temp
              local.set $min_x
            end

            global.get $g_old_x
            global.get $g_cursor_x
            i32.gt_s
            if (result i32)
              global.get $g_old_x
            else
              global.get $g_cursor_x
            end
            i32.const 6
            i32.add
            local.set $temp

            local.get $temp
            local.get $max_x
            i32.gt_s
            if
              local.get $temp
              local.set $max_x
            end

            global.get $g_old_y
            global.get $g_cursor_y
            i32.lt_s
            if (result i32)
              global.get $g_old_y
            else
              global.get $g_cursor_y
            end
            i32.const 6
            i32.sub
            local.set $temp

            local.get $temp
            local.get $min_y
            i32.lt_s
            if
              local.get $temp
              local.set $min_y
            end

            global.get $g_old_y
            global.get $g_cursor_y
            i32.gt_s
            if (result i32)
              global.get $g_old_y
            else
              global.get $g_cursor_y
            end
            i32.const 6
            i32.add
            local.set $temp

            local.get $temp
            local.get $max_y
            i32.gt_s
            if
              local.get $temp
              local.set $max_y
            end
          end

          ;; Handle Mouse Click (type == 1 - Button Down)
          local.get $ev_type
          i32.const 1
          i32.eq
          if
            i32.const 1
            local.set $need_redraw

            local.get $ev_x
            global.set $g_cursor_x
            local.get $ev_y
            global.set $g_cursor_y

            (block $click_processed
              ;; 1. If Start Menu is open:
              global.get $g_start_open
              if
                ;; Check if clicked inside Calculator item:
                global.get $g_cursor_x
                i32.const 12
                i32.ge_s
                global.get $g_cursor_x
                i32.const 196
                i32.le_s
                i32.and
                global.get $g_cursor_y
                global.get $g_height
                i32.const 130
                i32.sub
                i32.ge_s
                global.get $g_cursor_y
                global.get $g_height
                i32.const 94
                i32.sub
                i32.le_s
                i32.and
                i32.and
                if
                  ;; Unified launcher for Calculator
                  i32.const 12000 ;; "/CALC.WAS\00"
                  i32.const 12050 ;; "Calculator\00"
                  call $launch_or_focus
                end

                ;; Check if clicked inside Terminal item:
                global.get $g_cursor_x
                i32.const 12
                i32.ge_s
                global.get $g_cursor_x
                i32.const 196
                i32.le_s
                i32.and
                global.get $g_cursor_y
                global.get $g_height
                i32.const 92
                i32.sub
                i32.ge_s
                global.get $g_cursor_y
                global.get $g_height
                i32.const 56
                i32.sub
                i32.le_s
                i32.and
                i32.and
                if
                  ;; Unified launcher for Terminal
                  i32.const 12100 ;; "/TERM.WAS\00"
                  i32.const 12120 ;; "Terminal\00"
                  call $launch_or_focus
                end

                i32.const 0
                global.set $g_start_open
                br $click_processed
              end

              ;; 2. Check Taskbar Hit (cursor_y >= g_height - 36)
              global.get $g_cursor_y
              global.get $g_height
              i32.const 36
              i32.sub
              i32.ge_s
              if
                ;; Check Start Button: x in [4..80]
                global.get $g_cursor_x
                i32.const 4
                i32.ge_s
                global.get $g_cursor_x
                i32.const 80
                i32.le_s
                i32.and
                if
                  global.get $g_start_open
                  i32.eqz
                  global.set $g_start_open
                  br $click_processed
                end

                ;; Check Window Tabs on Taskbar
                call $get_window_count
                local.set $win_count
                i32.const 0
                local.set $idx
                (block $break_tab_loop
                  (loop $loop_tabs
                    local.get $idx
                    local.get $win_count
                    i32.ge_s
                    br_if $break_tab_loop

                    local.get $idx
                    i32.const 10000
                    call $get_window_info
                    drop

                    i32.const 10000
                    i32.load
                    local.set $win_id

                    i32.const 10000
                    i32.load offset=24
                    local.set $win_flags

                    ;; tab_x = 86 + idx * 145
                    i32.const 86
                    local.get $idx
                    i32.const 145
                    i32.mul
                    i32.add
                    local.set $tab_x

                    ;; Check cursor_x in [tab_x .. tab_x + 140]
                    global.get $g_cursor_x
                    local.get $tab_x
                    i32.ge_s
                    global.get $g_cursor_x
                    local.get $tab_x
                    i32.const 140
                    i32.add
                    i32.le_s
                    i32.and
                    if
                      local.get $win_flags
                      i32.const 8 ;; WOOS_WIN_FLAG_FOCUSED
                      i32.and
                      if
                        local.get $win_id
                        call $toggle_minimize
                        drop
                      else
                        local.get $win_id
                        call $bring_to_front
                        drop
                      end
                      br $click_processed
                    end

                    local.get $idx
                    i32.const 1
                    i32.add
                    local.set $idx
                    br $loop_tabs
                  )
                )
                br $click_processed
              end

              ;; 3. Check Windows on Desktop in reverse z-order (front to back!)
              call $get_window_count
              local.set $win_count

              local.get $win_count
              i32.const 1
              i32.sub
              local.set $idx
              (block $break_win_click
                (loop $loop_win_click
                  local.get $idx
                  i32.const 0
                  i32.lt_s
                  br_if $break_win_click

                  local.get $idx
                  i32.const 10000
                  call $get_window_info
                  drop

                  i32.const 10000
                  i32.load
                  local.set $win_id

                  i32.const 10000
                  i32.load offset=4
                  local.set $win_tid

                  i32.const 10000
                  i32.load offset=8
                  local.set $win_x

                  i32.const 10000
                  i32.load offset=12
                  local.set $win_y

                  i32.const 10000
                  i32.load offset=16
                  local.set $win_w

                  i32.const 10000
                  i32.load offset=20
                  local.set $win_h

                  i32.const 10000
                  i32.load offset=24
                  local.set $win_flags

                  ;; If window is minimized, skip it
                  local.get $win_flags
                  i32.const 1 ;; WOOS_WIN_FLAG_MINIMIZED
                  i32.and
                  if
                    local.get $idx
                    i32.const 1
                    i32.sub
                    local.set $idx
                    br $loop_win_click
                  end

                  ;; Check Custom Decor
                  local.get $win_flags
                  i32.const 4 ;; WOOS_WIN_FLAG_CUSTOM_DECOR
                  i32.and
                  if
                    global.get $g_cursor_x
                    local.get $win_x
                    i32.ge_s
                    global.get $g_cursor_x
                    local.get $win_x
                    local.get $win_w
                    i32.add
                    i32.le_s
                    i32.and
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.ge_s
                    global.get $g_cursor_y
                    local.get $win_y
                    local.get $win_h
                    i32.add
                    i32.le_s
                    i32.and
                    i32.and
                    if
                      local.get $win_id
                      call $bring_to_front
                      drop

                      local.get $win_tid
                      i32.const 1 ;; MOUSE_CLICK
                      global.get $g_cursor_x
                      local.get $win_x
                      i32.sub
                      global.get $g_cursor_y
                      local.get $win_y
                      i32.sub
                      i32.const 0
                      i32.const 0
                      i32.const 0
                      call $ipc_send
                      drop
                      br $click_processed
                    end
                  else
                    ;; Standard Windows Frame Checks:
                    ;; Caption buttons Y: [win_y - 22 .. win_y]
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.const 22
                    i32.sub
                    i32.ge_s
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.le_s
                    i32.and
                    if
                      ;; [X] Close: [win_x + win_w - 24 .. win_x + win_w]
                      global.get $g_cursor_x
                      local.get $win_x
                      local.get $win_w
                      i32.add
                      i32.const 24
                      i32.sub
                      i32.ge_s
                      global.get $g_cursor_x
                      local.get $win_x
                      local.get $win_w
                      i32.add
                      i32.le_s
                      i32.and
                      if
                        local.get $win_tid
                        i32.const 3 ;; MSG_CLOSE
                        i32.const 0
                        i32.const 0
                        i32.const 0
                        i32.const 0
                        i32.const 0
                        call $ipc_send
                        drop

                        local.get $win_id
                        call $destroy_window
                        drop

                        br $click_processed
                      end

                      ;; [□] Maximize: [win_x + win_w - 48 .. win_x + win_w - 25]
                      global.get $g_cursor_x
                      local.get $win_x
                      local.get $win_w
                      i32.add
                      i32.const 48
                      i32.sub
                      i32.ge_s
                      global.get $g_cursor_x
                      local.get $win_x
                      local.get $win_w
                      i32.add
                      i32.const 25
                      i32.sub
                      i32.le_s
                      i32.and
                      if
                        local.get $win_id
                        call $toggle_maximize
                        drop
                        br $click_processed
                      end

                      ;; [_] Minimize: [win_x + win_w - 72 .. win_x + win_w - 49]
                      global.get $g_cursor_x
                      local.get $win_x
                      local.get $win_w
                      i32.add
                      i32.const 72
                      i32.sub
                      i32.ge_s
                      global.get $g_cursor_x
                      local.get $win_x
                      local.get $win_w
                      i32.add
                      i32.const 49
                      i32.sub
                      i32.le_s
                      i32.and
                      if
                        local.get $win_id
                        call $toggle_minimize
                        drop
                        br $click_processed
                      end
                    end

                    ;; Titlebar Drag: [win_x - 3 .. win_x + win_w - 73], Y: [win_y - 25 .. win_y]
                    global.get $g_cursor_x
                    local.get $win_x
                    i32.const 3
                    i32.sub
                    i32.ge_s
                    global.get $g_cursor_x
                    local.get $win_x
                    local.get $win_w
                    i32.add
                    i32.const 73
                    i32.sub
                    i32.le_s
                    i32.and
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.const 25
                    i32.sub
                    i32.ge_s
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.le_s
                    i32.and
                    i32.and
                    if
                      local.get $win_id
                      call $bring_to_front
                      drop

                      ;; Only initiate drag if window is NOT maximized
                      local.get $win_flags
                      i32.const 2 ;; WOOS_WIN_FLAG_MAXIMIZED
                      i32.and
                      i32.eqz
                      if
                        local.get $win_id
                        global.set $g_drag_win_id

                        local.get $win_w
                        global.set $g_drag_win_w

                        local.get $win_h
                        global.set $g_drag_win_h

                        global.get $g_cursor_x
                        local.get $win_x
                        i32.sub
                        global.set $g_drag_off_x

                        global.get $g_cursor_y
                        local.get $win_y
                        i32.sub
                        global.set $g_drag_off_y
                      end
                      br $click_processed
                    end

                    ;; Client Area Click: [win_x .. win_x + win_w], Y: [win_y .. win_y + win_h]
                    global.get $g_cursor_x
                    local.get $win_x
                    i32.ge_s
                    global.get $g_cursor_x
                    local.get $win_x
                    local.get $win_w
                    i32.add
                    i32.le_s
                    i32.and
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.ge_s
                    global.get $g_cursor_y
                    local.get $win_y
                    local.get $win_h
                    i32.add
                    i32.le_s
                    i32.and
                    i32.and
                    if
                      local.get $win_id
                      call $bring_to_front
                      drop

                      local.get $win_tid
                      i32.const 1 ;; MOUSE_CLICK
                      global.get $g_cursor_x
                      local.get $win_x
                      i32.sub
                      global.get $g_cursor_y
                      local.get $win_y
                      i32.sub
                      i32.const 0
                      i32.const 0
                      i32.const 0
                      call $ipc_send
                      drop
                      br $click_processed
                    end
                  end

                  local.get $idx
                  i32.const 1
                  i32.sub
                  local.set $idx
                  br $loop_win_click
                )
              )

              ;; 4. Check Desktop Shortcut Icon (Calculator at 50,50, 64x64)
              global.get $g_cursor_x
              i32.const 50
              i32.ge_s
              global.get $g_cursor_x
              i32.const 114
              i32.le_s
              i32.and
              global.get $g_cursor_y
              i32.const 50
              i32.ge_s
              global.get $g_cursor_y
              i32.const 114
              i32.le_s
              i32.and
              i32.and
              if
                ;; Unified single-instance launcher for Calculator
                i32.const 12000 ;; "/CALC.WAS\00"
                i32.const 12050 ;; "Calculator\00"
                call $launch_or_focus
              end

              ;; 5. Check Desktop Shortcut Icon (Terminal at 150,50, 64x64)
              global.get $g_cursor_x
              i32.const 150
              i32.ge_s
              global.get $g_cursor_x
              i32.const 214
              i32.le_s
              i32.and
              global.get $g_cursor_y
              i32.const 50
              i32.ge_s
              global.get $g_cursor_y
              i32.const 114
              i32.le_s
              i32.and
              i32.and
              if
                ;; Unified single-instance launcher for Terminal
                i32.const 12100 ;; "/TERM.WAS\00"
                i32.const 12120 ;; "Terminal\00"
                call $launch_or_focus
              end

              ;; 6. Check Desktop Shortcut Icon (Notepad at 250,50, 64x64)
              global.get $g_cursor_x
              i32.const 250
              i32.ge_s
              global.get $g_cursor_x
              i32.const 314
              i32.le_s
              i32.and
              global.get $g_cursor_y
              i32.const 50
              i32.ge_s
              global.get $g_cursor_y
              i32.const 114
              i32.le_s
              i32.and
              i32.and
              if
                ;; Unified single-instance launcher for Notepad
                i32.const 12140 ;; "/NOTEPAD.WAS\00"
                i32.const 12160 ;; "Notepad\00"
                call $launch_or_focus
              end

              ;; 7. Check Desktop Shortcut Icon (Explorer at 350,50, 64x64)
              global.get $g_cursor_x
              i32.const 350
              i32.ge_s
              global.get $g_cursor_x
              i32.const 414
              i32.le_s
              i32.and
              global.get $g_cursor_y
              i32.const 50
              i32.ge_s
              global.get $g_cursor_y
              i32.const 114
              i32.le_s
              i32.and
              i32.and
              if
                ;; Unified single-instance launcher for Explorer
                i32.const 12180 ;; "/EXPLORER.WAS\00"
                i32.const 12200 ;; "Explorer\00"
                call $launch_or_focus
              end
            )
          end

          ;; Handle Mouse Release (type == 2 - Button Up)
          local.get $ev_type
          i32.const 2
          i32.eq
          if
            global.get $g_drag_win_id
            i32.const -1
            i32.ne
            if
              i32.const 1
              local.set $need_redraw
              i32.const -1
              global.set $g_drag_win_id
            end
          end

          ;; Handle Key Down (type == 4)
          local.get $ev_type
          i32.const 4
          i32.eq
          if
            call $get_focused_tid
            local.set $temp
            local.get $temp
            i32.const 0
            i32.ge_s
            if
              local.get $temp
              i32.const 4 ;; MSG_KEY_DOWN
              local.get $ev_x ;; ascii
              local.get $ev_y ;; scancode
              i32.const 0
              i32.const 0
              i32.const 0
              call $ipc_send
              drop
            end
          end

          ;; Handle Window Change (type == 6)
          local.get $ev_type
          i32.const 6
          i32.eq
          if
            i32.const 1
            local.set $need_redraw
            i32.const 1
            local.set $force_full_redraw
          end

          br $loop_events
        )
      )

      ;; Check heartbeat timer update (every 500ms)
      i32.const 1000
      call $get_status
      drop
      
      i32.const 1000
      i32.load
      local.set $ev_type
      
      local.get $ev_type
      i32.const 500
      i32.div_u
      global.get $g_last_uptime
      i32.ne
      if
        local.get $ev_type
        i32.const 500
        i32.div_u
        global.set $g_last_uptime

        ;; Fast incremental update: ONLY redraw the taskbar and present taskbar rect!
        call $draw_taskbar

        i32.const 65536
        i32.const 0
        global.get $g_height
        i32.const 36
        i32.sub
        global.get $g_width
        i32.const 36
        call $present_rect
        drop
      end

      local.get $need_redraw
      if
        local.get $force_full_redraw
        if
          i32.const 0
          global.set $g_clip_min_x
          global.get $g_width
          global.set $g_clip_max_x
          i32.const 0
          global.set $g_clip_min_y
          global.get $g_height
          global.set $g_clip_max_y
        else
          local.get $has_moved
          if
            ;; Set clipping to dirty rectangle
            local.get $min_x
            i32.const 0
            i32.lt_s
            if
              i32.const 0
              local.set $min_x
            end
            local.get $max_x
            global.get $g_width
            i32.gt_s
            if
              global.get $g_width
              local.set $max_x
            end

            local.get $min_y
            i32.const 0
            i32.lt_s
            if
              i32.const 0
              local.set $min_y
            end
            local.get $max_y
            global.get $g_height
            i32.gt_s
            if
              global.get $g_height
              local.set $max_y
            end

            local.get $min_x
            global.set $g_clip_min_x
            local.get $max_x
            global.set $g_clip_max_x
            local.get $min_y
            global.set $g_clip_min_y
            local.get $max_y
            global.set $g_clip_max_y
          else
            i32.const 0
            global.set $g_clip_min_x
            global.get $g_width
            global.set $g_clip_max_x
            i32.const 0
            global.set $g_clip_min_y
            global.get $g_height
            global.set $g_clip_max_y
          end
        end

        call $redraw_all

        ;; Reset clip bounds to full screen
        i32.const 0
        global.set $g_clip_min_x
        global.get $g_width
        global.set $g_clip_max_x
        i32.const 0
        global.set $g_clip_min_y
        global.get $g_height
        global.set $g_clip_max_y

        local.get $force_full_redraw
        if
          i32.const 65536
          i32.const 0
          i32.const 0
          global.get $g_width
          global.get $g_height
          call $present_rect
          drop
          i32.const 0
          local.set $has_moved
        else
          local.get $has_moved
          i32.eqz
          if
            i32.const 65536
            i32.const 0
            i32.const 0
            global.get $g_width
            global.get $g_height
            call $present_rect
            drop
          end
        end
      end

      ;; Present mouse movements / dragging dirty rect if any occurred
      local.get $has_moved
      if
        local.get $min_x
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.set $min_x
        end
        
        local.get $min_y
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.set $min_y
        end

        i32.const 65536
        local.get $min_x
        local.get $min_y
        local.get $max_x
        local.get $min_x
        i32.sub
        local.get $max_y
        local.get $min_y
        i32.sub
        call $present_rect
        drop
      end

      ;; Wait for next event (blocks thread)
      call $wait_event
      drop

      br $event_loop
    )
  )
)
