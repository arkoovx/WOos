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
  (import "env" "woos_graphics_draw_window_to_screen" (func $draw_window_to_screen (param i32) (result i32)))
  (import "env" "woos_graphics_draw_window_to_buffer" (func $draw_window_to_buffer (param i32 i32 i32 i32) (result i32)))

  (import "env" "woos_ipc_send" (func $ipc_send (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "woos_ipc_recv" (func $ipc_recv (param i32) (result i32)))
  (import "env" "woos_system_spawn" (func $system_spawn (param i32) (result i32)))

  ;; Import standard WASI functions
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))

  ;; Export linear memory: 90 pages (5.76 MB)
  (memory (export "memory") 90)

  ;; Data segment for Calculator filepath
  (data (i32.const 12000) "/CALC.WAS\00")

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

  ;; Draw solid rectangle onto buffer
  (func $draw_rect (param $x i32) (param $y i32) (param $w i32) (param $h i32) (param $color i32)
    (local $py i32)
    (local $px i32)
    (local $row_offset i32)
    (local $pixel_ptr i32)
    (local $x_end i32)
    (local $y_end i32)

    ;; x_end = x + w
    local.get $x
    local.get $w
    i32.add
    local.set $x_end

    ;; y_end = y + h
    local.get $y
    local.get $h
    i32.add
    local.set $y_end

    ;; Clip x against g_clip_min_x
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

    ;; Clip y against g_clip_min_y
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

    ;; Loop py from y to y_end
    local.get $y
    local.set $py
    (block $break_y
      (loop $loop_y
        local.get $py
        local.get $y_end
        i32.ge_s
        br_if $break_y

        ;; row_offset = py * pitch
        local.get $py
        global.get $g_pitch
        i32.mul
        local.set $row_offset

        ;; Loop px from x to x_end
        local.get $x
        local.set $px
        (block $break_x
          (loop $loop_x
            local.get $px
            local.get $x_end
            i32.ge_s
            br_if $break_x

            ;; pixel_ptr = 65536 + row_offset + px * 4
            i32.const 65536
            local.get $row_offset
            i32.add
            local.get $px
            i32.const 4
            i32.mul
            i32.add
            local.set $pixel_ptr

            ;; Store color in buffer
            local.get $pixel_ptr
            local.get $color
            i32.store

            ;; px++
            local.get $px
            i32.const 1
            i32.add
            local.set $px
            br $loop_x
          )
        )

        ;; py++
        local.get $py
        i32.const 1
        i32.add
        local.set $py
        br $loop_y
      )
    )
  )

  ;; Renders mouse cursor (crosshair style)
  (func $draw_cursor (param $cx i32) (param $cy i32) (param $color i32)
    ;; Draw horizontal line (width=11, height=2)
    local.get $cx
    i32.const 5
    i32.sub
    local.get $cy
    i32.const 1
    i32.sub
    i32.const 11
    i32.const 2
    local.get $color
    call $draw_rect

    ;; Draw vertical line (width=2, height=11)
    local.get $cx
    i32.const 1
    i32.sub
    local.get $cy
    i32.const 5
    i32.sub
    i32.const 2
    i32.const 11
    local.get $color
    call $draw_rect
  )

  ;; Save 12x12 pixels under cx, cy to address 2000
  (func $save_cursor_underlay (param $cx i32) (param $cy i32)
    (local $py i32)
    (local $px i32)
    (local $saved_ptr i32)
    (local $pixel_ptr i32)
    (local $row_offset i32)
    (local $tx i32)
    (local $ty i32)

    i32.const 2000
    local.set $saved_ptr

    i32.const 0
    local.set $py
    (block $break_y
      (loop $loop_y
        local.get $py
        i32.const 12
        i32.ge_s
        br_if $break_y

        local.get $cy
        i32.const 6
        i32.sub
        local.get $py
        i32.add
        local.set $ty

        ;; Clip ty
        local.get $ty
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.set $ty
        end
        local.get $ty
        global.get $g_height
        i32.ge_s
        if
          global.get $g_height
          i32.const 1
          i32.sub
          local.set $ty
        end

        local.get $ty
        global.get $g_pitch
        i32.mul
        local.set $row_offset

        i32.const 0
        local.set $px
        (block $break_x
          (loop $loop_x
            local.get $px
            i32.const 12
            i32.ge_s
            br_if $break_x

            local.get $cx
            i32.const 6
            i32.sub
            local.get $px
            i32.add
            local.set $tx

            ;; Clip tx
            local.get $tx
            i32.const 0
            i32.lt_s
            if
              i32.const 0
              local.set $tx
            end
            local.get $tx
            global.get $g_width
            i32.ge_s
            if
              global.get $g_width
              i32.const 1
              i32.sub
              local.set $tx
            end

            ;; pixel_ptr = 65536 + row_offset + tx * 4
            i32.const 65536
            local.get $row_offset
            i32.add
            local.get $tx
            i32.const 4
            i32.mul
            i32.add
            local.set $pixel_ptr

            ;; Save pixel
            local.get $saved_ptr
            local.get $pixel_ptr
            i32.load
            i32.store

            local.get $saved_ptr
            i32.const 4
            i32.add
            local.set $saved_ptr

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

  ;; Restore 12x12 pixels under cx, cy from address 2000
  (func $restore_cursor_underlay (param $cx i32) (param $cy i32)
    (local $py i32)
    (local $px i32)
    (local $saved_ptr i32)
    (local $pixel_ptr i32)
    (local $row_offset i32)
    (local $tx i32)
    (local $ty i32)

    i32.const 2000
    local.set $saved_ptr

    i32.const 0
    local.set $py
    (block $break_y
      (loop $loop_y
        local.get $py
        i32.const 12
        i32.ge_s
        br_if $break_y

        local.get $cy
        i32.const 6
        i32.sub
        local.get $py
        i32.add
        local.set $ty

        ;; Clip ty
        local.get $ty
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.set $ty
        end
        local.get $ty
        global.get $g_height
        i32.ge_s
        if
          global.get $g_height
          i32.const 1
          i32.sub
          local.set $ty
        end

        local.get $ty
        global.get $g_pitch
        i32.mul
        local.set $row_offset

        i32.const 0
        local.set $px
        (block $break_x
          (loop $loop_x
            local.get $px
            i32.const 12
            i32.ge_s
            br_if $break_x

            local.get $cx
            i32.const 6
            i32.sub
            local.get $px
            i32.add
            local.set $tx

            ;; Clip tx
            local.get $tx
            i32.const 0
            i32.lt_s
            if
              i32.const 0
              local.set $tx
            end
            local.get $tx
            global.get $g_width
            i32.ge_s
            if
              global.get $g_width
              i32.const 1
              i32.sub
              local.set $tx
            end

            ;; pixel_ptr = 65536 + row_offset + tx * 4
            i32.const 65536
            local.get $row_offset
            i32.add
            local.get $tx
            i32.const 4
            i32.mul
            i32.add
            local.set $pixel_ptr

            ;; Restore pixel
            local.get $pixel_ptr
            local.get $saved_ptr
            i32.load
            i32.store

            local.get $saved_ptr
            i32.const 4
            i32.add
            local.set $saved_ptr

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

  ;; Draw taskbar status dashboard
  (func $draw_status_indicators
    (local $uptime_ms i32)
    (local $total_pages i32)
    (local $free_pages i32)
    (local $used_pages i32)
    (local $used_mem_width i32)
    (local $heap_used i64)
    (local $heap_free i64)
    (local $used_heap_width i32)
    (local $net_link_up i32)
    (local $heartbeat_color i32)

    ;; Poll system status into address 1000
    i32.const 1000
    call $get_status
    drop

    ;; Read status struct fields
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

    ;; 1. Heartbeat
    local.get $uptime_ms
    i32.const 500
    i32.div_u
    global.get $g_last_uptime
    i32.ne
    if
      local.get $uptime_ms
      i32.const 500
      i32.div_u
      global.set $g_last_uptime
    end

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

    i32.const 10
    i32.const 1000
    i32.const 16
    i32.const 16
    local.get $heartbeat_color
    call $draw_rect

    ;; 2. Network Status
    i32.const 40
    i32.const 1000
    i32.const 16
    i32.const 16
    local.get $net_link_up
    if (result i32)
      i32.const 0x2ECC71
    else
      i32.const 0xE74C3C
    end
    call $draw_rect

    ;; 3. Memory usage bar
    i32.const 80
    i32.const 1000
    i32.const 200
    i32.const 16
    i32.const 0x7F8C8D
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
      i32.const 200
      i32.mul
      local.get $total_pages
      i32.div_u
      local.set $used_mem_width
    else
      i32.const 0
      local.set $used_mem_width
    end

    local.get $used_mem_width
    i32.const 0
    i32.gt_s
    if
      i32.const 80
      i32.const 1000
      local.get $used_mem_width
      i32.const 16
      i32.const 0x9B59B6
      call $draw_rect
    end

    ;; 4. Heap usage bar
    i32.const 300
    i32.const 1000
    i32.const 200
    i32.const 16
    i32.const 0x7F8C8D
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
      i64.const 200
      i64.mul
      local.get $heap_free
      i64.div_u
      i32.wrap_i64
      local.set $used_heap_width
    else
      i32.const 0
      local.set $used_heap_width
    end

    local.get $used_heap_width
    i32.const 0
    i32.gt_s
    if
      i32.const 300
      i32.const 1000
      local.get $used_heap_width
      i32.const 16
      i32.const 0x3498DB
      call $draw_rect
    end
  )

  ;; Renders wallpaper, windows, taskbar, saves underlay, draws cursor, flushes
  (func $redraw_all
    (local $win_count i32)
    (local $idx i32)
    (local $win_id i32)
    (local $win_tid i32)
    (local $win_x i32)
    (local $win_y i32)
    (local $win_w i32)
    (local $win_h i32)

    ;; 1. Draw wallpaper background
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    i32.const 0x2C3E50
    call $draw_rect

    ;; 2. Draw Calculator icon
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

    ;; 3. Iterate and draw windows
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

        ;; Draw window frame border
        local.get $win_x
        i32.const 3
        i32.sub
        local.get $win_y
        i32.const 24
        i32.sub
        local.get $win_w
        i32.const 6
        i32.add
        local.get $win_h
        i32.const 27
        i32.add
        i32.const 0x7F8C8D
        call $draw_rect

        ;; Draw title bar
        local.get $win_x
        local.get $win_y
        i32.const 20
        i32.sub
        local.get $win_w
        i32.const 20
        i32.const 0x2980B9
        call $draw_rect

        ;; Draw close button
        local.get $win_x
        local.get $win_w
        i32.add
        i32.const 18
        i32.sub
        local.get $win_y
        i32.const 18
        i32.sub
        i32.const 16
        i32.const 16
        i32.const 0xC0392B
        call $draw_rect

        ;; Draw window body content
        local.get $win_id
        i32.const 65536
        global.get $g_width
        global.get $g_height
        call $draw_window_to_buffer
        drop


        local.get $idx
        i32.const 1
        i32.add
        local.set $idx
        br $loop_win
      )
    )

    ;; 4. Draw taskbar
    i32.const 0
    i32.const 992
    global.get $g_width
    i32.const 32
    i32.const 0x34495E
    call $draw_rect

    call $draw_status_indicators

    ;; 5. Save underlay before drawing cursor!
    global.get $g_cursor_x
    global.get $g_cursor_y
    call $save_cursor_underlay

    ;; 6. Draw mouse cursor
    global.get $g_cursor_x
    global.get $g_cursor_y
    i32.const 0xE74C3C
    call $draw_cursor

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
    (local $inside_tb i32)
    (local $inside_client i32)
    (local $app_running i32)

    
    (local $has_moved i32)
    (local $min_x i32)
    (local $max_x i32)
    (local $min_y i32)
    (local $max_y i32)
    (local $temp i32)
    (local $need_redraw i32)

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

            ;; Erase old cursor (restore underlay)
            global.get $g_old_x
            global.get $g_old_y
            call $restore_cursor_underlay

            ;; If we are dragging a window, update its position
            global.get $g_drag_win_id
            i32.const -1
            i32.ne
            if
              global.get $g_drag_win_id
              global.get $g_cursor_x
              global.get $g_drag_off_x
              i32.sub
              global.get $g_cursor_y
              global.get $g_drag_off_y
              i32.sub
              call $set_window_pos
              drop
              
              ;; Load win_w, win_h of the dragged window
              i32.const 0
              local.set $idx
              call $get_window_count
              local.set $win_count
              (block $break_find_drag
                (loop $loop_find_drag
                  local.get $idx
                  local.get $win_count
                  i32.ge_s
                  br_if $break_find_drag
                  
                  local.get $idx
                  i32.const 10000
                  call $get_window_info
                  drop
                  
                  i32.const 10000
                  i32.load
                  global.get $g_drag_win_id
                  i32.eq
                  if
                    i32.const 10000
                    i32.load offset=16
                    local.set $win_w
                    i32.const 10000
                    i32.load offset=20
                    local.set $win_h
                    br $break_find_drag
                  end
                  
                  local.get $idx
                  i32.const 1
                  i32.add
                  local.set $idx
                  br $loop_find_drag
                )
              )

              ;; Now expand min_x, max_x, min_y, max_y to include the drag bounds!
              ;; 1. min_x = min(min_x, min(g_old_x, g_cursor_x) - g_drag_off_x - 3)
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

              ;; 2. max_x = max(max_x, max(g_old_x, g_cursor_x) - g_drag_off_x + win_w + 3)
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
              local.get $win_w
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

              ;; 3. min_y = min(min_y, min(g_old_y, g_cursor_y) - g_drag_off_y - 24)
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
              i32.const 24
              i32.sub
              local.set $temp
              
              local.get $temp
              local.get $min_y
              i32.lt_s
              if
                local.get $temp
                local.set $min_y
              end

              ;; 4. max_y = max(max_y, max(g_old_y, g_cursor_y) - g_drag_off_y + win_h + 3)
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
              local.get $win_h
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

            ;; Save underlay at new cursor pos
            global.get $g_cursor_x
            global.get $g_cursor_y
            call $save_cursor_underlay

            ;; Draw cursor
            global.get $g_cursor_x
            global.get $g_cursor_y
            i32.const 0xE74C3C
            call $draw_cursor

            ;; Mark that we moved
            i32.const 1
            local.set $has_moved

            ;; Update min_x
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

            ;; Update max_x
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

            ;; Update min_y
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

            ;; Update max_y
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

            ;; Check if clicked Calculator icon: x=50..114, y=50..114
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
              ;; Loop windows and check if there's any spawned app window
              i32.const 0
              local.set $idx
              i32.const 0
              local.set $app_running
              
              call $get_window_count
              local.set $win_count
              
              (block $break_check_app
                (loop $loop_check_app
                  local.get $idx
                  local.get $win_count
                  i32.ge_s
                  br_if $break_check_app
                  
                  local.get $idx
                  i32.const 10000
                  call $get_window_info
                  drop
                  
                  i32.const 10000
                  i32.load offset=4 ;; thread_id
                  i32.const 1 ;; compositor tid
                  i32.ne
                  if
                    i32.const 1
                    local.set $app_running
                    br $break_check_app
                  end
                  
                  local.get $idx
                  i32.const 1
                  i32.add
                  local.set $idx
                  br $loop_check_app
                )
              )
              
              local.get $app_running
              i32.eqz
              if
                i32.const 12000
                call $system_spawn
                drop
              end
            else
              ;; Check click on windows
              call $get_window_count
              local.set $win_count

              i32.const 0
              local.set $idx
              (block $break_click_check
                (loop $loop_click_check
                  local.get $idx
                  local.get $win_count
                  i32.ge_s
                  br_if $break_click_check

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

                  ;; Title bar check
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
                  i32.const 20
                  i32.sub
                  i32.ge_s
                  global.get $g_cursor_y
                  local.get $win_y
                  i32.le_s
                  i32.and
                  i32.and
                  local.set $inside_tb

                  local.get $inside_tb
                  if
                    local.get $win_id
                    global.set $g_drag_win_id
                    global.get $g_cursor_x
                    local.get $win_x
                    i32.sub
                    global.set $g_drag_off_x
                    global.get $g_cursor_y
                    local.get $win_y
                    i32.sub
                    global.set $g_drag_off_y
                    
                    br $break_click_check
                  end

                  ;; Client area check
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
                  local.set $inside_client

                  local.get $inside_client
                  if
                    ;; Send click over IPC
                    local.get $win_tid
                    i32.const 1
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
                    
                    br $break_click_check
                  end

                  local.get $idx
                  i32.const 1
                  i32.add
                  local.set $idx
                  br $loop_click_check
                )
              )
            end
          end

          ;; Handle Mouse Release (type == 2 - Button Up)
          local.get $ev_type
          i32.const 2
          i32.eq
          if
            i32.const 1
            local.set $need_redraw

            i32.const -1
            global.set $g_drag_win_id
          end

          br $loop_events
        )
      )

      ;; Check heartbeat
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
        ;; Check if cursor intersects taskbar: Y >= 980
        global.get $g_cursor_y
        i32.const 980
        i32.ge_s
        if
          ;; Restore underlay first so we don't double-save the cursor
          global.get $g_cursor_x
          global.get $g_cursor_y
          call $restore_cursor_underlay
        end

        ;; Draw taskbar background
        i32.const 0
        i32.const 992
        global.get $g_width
        i32.const 32
        i32.const 0x34495E
        call $draw_rect

        ;; Draw status indicators (automatically updates g_last_uptime)
        call $draw_status_indicators

        ;; If cursor intersects taskbar, save the new underlay and redraw the cursor
        global.get $g_cursor_y
        i32.const 980
        i32.ge_s
        if
          global.get $g_cursor_x
          global.get $g_cursor_y
          call $save_cursor_underlay

          global.get $g_cursor_x
          global.get $g_cursor_y
          i32.const 0xE74C3C
          call $draw_cursor
        end

        ;; Present taskbar area
        i32.const 65536
        i32.const 0
        i32.const 992
        global.get $g_width
        i32.const 32
        call $present_rect
        drop
      end

      local.get $need_redraw
      if
        ;; Set clipping bounds before redrawing
        local.get $has_moved
        if
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

        call $redraw_all

        ;; Restore clip bounds to full screen
        i32.const 0
        global.set $g_clip_min_x
        global.get $g_width
        global.set $g_clip_max_x
        i32.const 0
        global.set $g_clip_min_y
        global.get $g_height
        global.set $g_clip_max_y
        
        local.get $has_moved
        i32.eqz
        if
          ;; Present full screen
          i32.const 65536
          i32.const 0
          i32.const 0
          global.get $g_width
          global.get $g_height
          call $present_rect
          drop
        end
      end

      ;; Present mouse movements dirty rect if any occurred
      local.get $has_moved
      if
        ;; Clip min_x to 0
        local.get $min_x
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.set $min_x
        end
        
        ;; Clip min_y to 0
        local.get $min_y
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.set $min_y
        end

        ;; Call present_rect (min_x, min_y, max_x - min_x, max_y - min_y)
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
