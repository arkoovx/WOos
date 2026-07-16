(module
  ;; Imports
  (import "env" "woos_graphics_create_window" (func $create_window (param i32 i32) (result i32)))
  (import "env" "woos_graphics_blit_window" (func $blit_window (param i32 i32 i32 i32) (result i32)))
  (import "env" "woos_ipc_recv" (func $ipc_recv (param i32) (result i32)))
  (import "env" "woos_ipc_wait_message" (func $wait_message (result i32)))
  (import "env" "woos_thread_get_id" (func $get_thread_id (result i32)))

  ;; Export memory (8 pages = 512KB)
  (memory (export "memory") 8)

  ;; Globals
  (global $g_window_id (mut i32) (i32.const -1))
  (global $g_display_value (mut i32) (i32.const 0))
  (global $g_highlight_btn (mut i32) (i32.const -1)) ;; ID of button currently highlighted (-1 if none)

  ;; Width, Height of Calculator Window
  (global $g_width i32 (i32.const 240))
  (global $g_height i32 (i32.const 320))

  ;; Draw solid rectangle onto local buffer
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

    local.get $y
    local.set $py
    (block $break_y
      (loop $loop_y
        local.get $py
        local.get $y_end
        i32.ge_s
        br_if $break_y

        local.get $py
        global.get $g_width
        i32.mul
        i32.const 4
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

            ;; Buffer starts at 65536
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

  ;; Renders a 3x5 pixel digit at (x, y) where each pixel is block_size x block_size
  (func $draw_digit (param $digit i32) (param $x i32) (param $y i32) (param $size i32) (param $color i32)
    (local $mask i32)
    (local $row i32)
    (local $col i32)
    (local $bit_idx i32)
    (local $bit_set i32)

    ;; Digits bitmasks: 0-9 represented as 15-bit values (5 rows of 3 columns)
    ;; E.g. 0: 111 101 101 101 111 = 0x7B6F
    ;; 1: 010 010 010 010 010 = 0x1249
    ;; 2: 111 001 111 100 111 = 0x73E7
    ;; 3: 111 001 111 001 111 = 0x73CF
    ;; 4: 101 101 111 001 001 = 0x5B09
    ;; 5: 111 100 111 001 111 = 0x7CFF
    ;; 6: 111 100 111 101 111 = 0x7D6F
    ;; 7: 111 001 001 001 001 = 0x7124
    ;; 8: 111 101 111 101 111 = 0x7D6F or similar
    ;; 9: 111 101 111 001 111 = 0x7DCE
    
    i32.const 0
    local.set $mask

    local.get $digit
    i32.const 0
    i32.eq
    if
      i32.const 0x7B6F
      local.set $mask
    end
    local.get $digit
    i32.const 1
    i32.eq
    if
      i32.const 0x2492
      local.set $mask
    end
    local.get $digit
    i32.const 2
    i32.eq
    if
      i32.const 0x73E7
      local.set $mask
    end
    local.get $digit
    i32.const 3
    i32.eq
    if
      i32.const 0x73CF
      local.set $mask
    end
    local.get $digit
    i32.const 4
    i32.eq
    if
      i32.const 0x5B29
      local.set $mask
    end
    local.get $digit
    i32.const 5
    i32.eq
    if
      i32.const 0x79CF
      local.set $mask
    end
    local.get $digit
    i32.const 6
    i32.eq
    if
      i32.const 0x79EF
      local.set $mask
    end
    local.get $digit
    i32.const 7
    i32.eq
    if
      i32.const 0x7249
      local.set $mask
    end
    local.get $digit
    i32.const 8
    i32.eq
    if
      i32.const 0x7BEF
      local.set $mask
    end
    local.get $digit
    i32.const 9
    i32.eq
    if
      i32.const 0x7BCF
      local.set $mask
    end

    ;; Loop rows 0..4
    i32.const 0
    local.set $row
    (block $break_y
      (loop $loop_y
        local.get $row
        i32.const 5
        i32.ge_s
        br_if $break_y

        ;; Loop cols 0..2
        i32.const 0
        local.set $col
        (block $break_x
          (loop $loop_x
            local.get $col
            i32.const 3
            i32.ge_s
            br_if $break_x

            ;; bit_idx = 14 - (row * 3 + col)
            i32.const 14
            local.get $row
            i32.const 3
            i32.mul
            local.get $col
            i32.add
            i32.sub
            local.set $bit_idx

            ;; bit_set = (mask >> bit_idx) & 1
            local.get $mask
            local.get $bit_idx
            i32.shr_u
            i32.const 1
            i32.and
            local.set $bit_set

            local.get $bit_set
            if
              ;; Draw block at (x + col*size, y + row*size)
              local.get $x
              local.get $col
              local.get $size
              i32.mul
              i32.add
              
              local.get $y
              local.get $row
              local.get $size
              i32.mul
              i32.add

              local.get $size
              local.get $size
              local.get $color
              call $draw_rect
            end

            local.get $col
            i32.const 1
            i32.add
            local.set $col
            br $loop_x
          )
        )

        local.get $row
        i32.const 1
        i32.add
        local.set $row
        br $loop_y
      )
    )
  )

  ;; Redraw entire calculator interface
  (func $redraw_ui
    (local $c_btn1 i32)
    (local $c_btn2 i32)
    (local $c_btn3 i32)
    (local $c_btn4 i32)
    (local $c_btn5 i32)
    (local $c_btn6 i32)
    (local $c_btn_c i32)

    ;; Colors
    ;; Normal gray button: 0xBDC3C7
    ;; Highlighted button: 0x3498DB
    ;; Clear button: 0xE74C3C
    i32.const 0xBDC3C7
    local.set $c_btn1
    i32.const 0xBDC3C7
    local.set $c_btn2
    i32.const 0xBDC3C7
    local.set $c_btn3
    i32.const 0xBDC3C7
    local.set $c_btn4
    i32.const 0xBDC3C7
    local.set $c_btn5
    i32.const 0xBDC3C7
    local.set $c_btn6
    i32.const 0xC0392B
    local.set $c_btn_c

    ;; Update button colors based on highlight global
    global.get $g_highlight_btn
    i32.const 1
    i32.eq
    if
      i32.const 0x3498DB
      local.set $c_btn1
    end
    global.get $g_highlight_btn
    i32.const 2
    i32.eq
    if
      i32.const 0x3498DB
      local.set $c_btn2
    end
    global.get $g_highlight_btn
    i32.const 3
    i32.eq
    if
      i32.const 0x3498DB
      local.set $c_btn3
    end
    global.get $g_highlight_btn
    i32.const 4
    i32.eq
    if
      i32.const 0x3498DB
      local.set $c_btn4
    end
    global.get $g_highlight_btn
    i32.const 5
    i32.eq
    if
      i32.const 0x3498DB
      local.set $c_btn5
    end
    global.get $g_highlight_btn
    i32.const 6
    i32.eq
    if
      i32.const 0x3498DB
      local.set $c_btn6
    end
    global.get $g_highlight_btn
    i32.const 0
    i32.eq
    if
      i32.const 0xE74C3C
      local.set $c_btn_c
    end

    ;; 1. Draw calculator background panel (dark slate 0x2C3E50)
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    i32.const 0x2C3E50
    call $draw_rect

    ;; 2. Draw display box (white 0xECF0F1)
    i32.const 10
    i32.const 10
    i32.const 220
    i32.const 60
    i32.const 0xECF0F1
    call $draw_rect

    ;; 3. Draw current value onto display (at center 105, 15, block size 6)
    global.get $g_display_value
    i32.const 110
    i32.const 20
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; 4. Draw Row 1 of buttons: C, 1, 2
    ;; Clear button 'C' at (10, 80)
    i32.const 10
    i32.const 80
    i32.const 60
    i32.const 50
    local.get $c_btn_c
    call $draw_rect

    ;; Button '1' at (90, 80)
    i32.const 90
    i32.const 80
    i32.const 60
    i32.const 50
    local.get $c_btn1
    call $draw_rect
    i32.const 1
    i32.const 110
    i32.const 90
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; Button '2' at (170, 80)
    i32.const 170
    i32.const 80
    i32.const 60
    i32.const 50
    local.get $c_btn2
    call $draw_rect
    i32.const 2
    i32.const 190
    i32.const 90
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; 5. Draw Row 2 of buttons: 3, 4, 5
    ;; Button '3' at (10, 150)
    i32.const 10
    i32.const 150
    i32.const 60
    i32.const 50
    local.get $c_btn3
    call $draw_rect
    i32.const 3
    i32.const 30
    i32.const 160
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; Button '4' at (90, 150)
    i32.const 90
    i32.const 150
    i32.const 60
    i32.const 50
    local.get $c_btn4
    call $draw_rect
    i32.const 4
    i32.const 110
    i32.const 160
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; Button '5' at (170, 150)
    i32.const 170
    i32.const 150
    i32.const 60
    i32.const 50
    local.get $c_btn5
    call $draw_rect
    i32.const 5
    i32.const 190
    i32.const 160
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; 6. Draw Row 3 of buttons: 6, empty, empty
    ;; Button '6' at (90, 220)
    i32.const 90
    i32.const 220
    i32.const 60
    i32.const 50
    local.get $c_btn6
    call $draw_rect
    i32.const 6
    i32.const 110
    i32.const 230
    i32.const 6
    i32.const 0x2C3E50
    call $draw_digit

    ;; Blit buffer to window
    global.get $g_window_id
    i32.const 65536
    global.get $g_width
    global.get $g_height
    call $blit_window
    drop
  )

  ;; Entry point _start
  (func (export "_start")
    (local $event_popped i32)
    (local $ev_sender i32)
    (local $ev_type i32)
    (local $ev_x i32)
    (local $ev_y i32)
    (local $click_btn i32)
    (local $need_redraw i32)

    ;; 1. Register calculator window
    global.get $g_width
    global.get $g_height
    call $create_window
    global.set $g_window_id

    global.get $g_window_id
    i32.const 0
    i32.lt_s
    if
      unreachable
    end

    ;; Initial UI redraw
    call $redraw_ui

    ;; 2. Main event loop
    (loop $event_loop
      i32.const 0
      local.set $need_redraw

      (block $break_events
        (loop $loop_events
          ;; Poll next IPC message (struct resides at address 32)
          i32.const 32
          call $ipc_recv
          local.set $event_popped

          local.get $event_popped
          i32.eqz
          br_if $break_events

          ;; Load message fields
          i32.const 32
          i32.load
          local.set $ev_sender

          i32.const 40
          i32.load
          local.set $ev_type

          i32.const 44
          i32.load
          local.set $ev_x

          i32.const 48
          i32.load
          local.set $ev_y

          ;; If type == MSG_MOUSE_CLICK (1)
          local.get $ev_type
          i32.const 1
          i32.eq
          if
            ;; Reset click button
            i32.const -1
            local.set $click_btn

            ;; Check click button bounds:
            ;; Button C: x=10..70, y=80..130
            local.get $ev_x
            i32.const 10
            i32.ge_s
            local.get $ev_x
            i32.const 70
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 80
            i32.ge_s
            local.get $ev_y
            i32.const 130
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 0
              local.set $click_btn
            end

            ;; Button 1: x=90..150, y=80..130
            local.get $ev_x
            i32.const 90
            i32.ge_s
            local.get $ev_x
            i32.const 150
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 80
            i32.ge_s
            local.get $ev_y
            i32.const 130
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 1
              local.set $click_btn
            end

            ;; Button 2: x=170..230, y=80..130
            local.get $ev_x
            i32.const 170
            i32.ge_s
            local.get $ev_x
            i32.const 230
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 80
            i32.ge_s
            local.get $ev_y
            i32.const 130
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 2
              local.set $click_btn
            end

            ;; Button 3: x=10..70, y=150..200
            local.get $ev_x
            i32.const 10
            i32.ge_s
            local.get $ev_x
            i32.const 70
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 150
            i32.ge_s
            local.get $ev_y
            i32.const 200
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 3
              local.set $click_btn
            end

            ;; Button 4: x=90..150, y=150..200
            local.get $ev_x
            i32.const 90
            i32.ge_s
            local.get $ev_x
            i32.const 150
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 150
            i32.ge_s
            local.get $ev_y
            i32.const 200
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 4
              local.set $click_btn
            end

            ;; Button 5: x=170..230, y=150..200
            local.get $ev_x
            i32.const 170
            i32.ge_s
            local.get $ev_x
            i32.const 230
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 150
            i32.ge_s
            local.get $ev_y
            i32.const 200
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 5
              local.set $click_btn
            end

            ;; Button 6: x=90..150, y=220..270
            local.get $ev_x
            i32.const 90
            i32.ge_s
            local.get $ev_x
            i32.const 150
            i32.le_s
            i32.and
            local.get $ev_y
            i32.const 220
            i32.ge_s
            local.get $ev_y
            i32.const 270
            i32.le_s
            i32.and
            i32.and
            if
              i32.const 6
              local.set $click_btn
            end

            ;; Handle clicked button
            local.get $click_btn
            i32.const -1
            i32.ne
            if
              local.get $click_btn
              global.set $g_display_value
              local.get $click_btn
              global.set $g_highlight_btn

              ;; Redraw with highlight
              call $redraw_ui

              ;; Keep highlight for a short moment, then reset it
              i32.const -1
              global.set $g_highlight_btn
              i32.const 1
              local.set $need_redraw
            end
          end

          br $loop_events
        )
      )

      local.get $need_redraw
      if
        call $redraw_ui
      end

      ;; Wait for next IPC message (blocks thread)
      call $wait_message
      drop

      br $event_loop
    )
  )
)
