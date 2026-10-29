(module
  ;; Imports from WoOS host environment
  (import "env" "woos_graphics_create_window_ex" (func $create_window_ex (param i32 i32 i32 i32) (result i32)))
  (import "env" "woos_graphics_create_window" (func $create_window (param i32 i32) (result i32)))
  (import "env" "woos_graphics_blit_window" (func $blit_window (param i32 i32 i32 i32) (result i32)))
  (import "env" "woos_ipc_recv" (func $ipc_recv (param i32) (result i32)))
  (import "env" "woos_ipc_wait_message" (func $wait_message (result i32)))
  (import "env" "woos_thread_get_id" (func $get_thread_id (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))

  ;; Export linear memory: 8 pages = 512 KB
  ;; 0..31: Reserved
  ;; 32..63: IPC message struct
  ;; 64..127: Scratch digit buffer for display formatting
  ;; 200..230: Title string "Calculator\00"
  ;; 65536..: Pixel backbuffer (240 x 320 x 4 bytes = 307200 bytes)
  (memory (export "memory") 8)
  (data (i32.const 200) "Calculator\00")

  ;; Window Dimensions
  (global $g_window_id (mut i32) (i32.const -1))
  (global $g_width i32 (i32.const 240))
  (global $g_height i32 (i32.const 320))

  ;; Calculator Engine State
  (global $g_display_value (mut i32) (i32.const 0))
  (global $g_accumulator (mut i32) (i32.const 0))
  (global $g_pending_op (mut i32) (i32.const 0))  ;; 0: None, 1: +, 2: -, 3: *, 4: /
  (global $g_has_error (mut i32) (i32.const 0))   ;; 1 if divide-by-zero or overflow
  (global $g_input_mode (mut i32) (i32.const 0))  ;; 0: waiting for first digit, 1: appending digits
  (global $g_highlight_btn (mut i32) (i32.const -1))

  ;; =========================================================================
  ;; DRAW RECTANGLE: clipped to (0, 0, g_width, g_height)
  ;; =========================================================================
  (func $draw_rect (param $x i32) (param $y i32) (param $w i32) (param $h i32) (param $color i32)
    (local $py i32)
    (local $px i32)
    (local $row_offset i32)
    (local $pixel_ptr i32)
    (local $x_end i32)
    (local $y_end i32)

    ;; x_end = min(x + w, g_width)
    local.get $x
    local.get $w
    i32.add
    local.set $x_end
    local.get $x_end
    global.get $g_width
    i32.gt_s
    if
      global.get $g_width
      local.set $x_end
    end

    ;; y_end = min(y + h, g_height)
    local.get $y
    local.get $h
    i32.add
    local.set $y_end
    local.get $y_end
    global.get $g_height
    i32.gt_s
    if
      global.get $g_height
      local.set $y_end
    end

    ;; Clamp start x, y to >= 0
    local.get $x
    i32.const 0
    i32.lt_s
    if
      i32.const 0
      local.set $x
    end

    local.get $y
    i32.const 0
    i32.lt_s
    if
      i32.const 0
      local.set $y
    end

    local.get $y
    local.set $py
    (block $break_y
      (loop $loop_y
        local.get $py
        local.get $y_end
        i32.ge_s
        br_if $break_y

        ;; row_offset = py * g_width * 4
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

            ;; pixel_ptr = 65536 + row_offset + px * 4
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

  ;; =========================================================================
  ;; 3x5 FONT GLYPH RENDERER
  ;; Supports:
  ;; 0..9: Digits
  ;; 10: 'C' (Clear)
  ;; 11: '<-' (Backspace)
  ;; 12: '+/-' (Plus/Minus)
  ;; 13: '/' (Divide)
  ;; 14: '*' (Multiply)
  ;; 15: '-' (Minus)
  ;; 16: '+' (Plus)
  ;; 17: '=' (Equals)
  ;; 18: 'E' (Error)
  ;; =========================================================================
  (func $draw_glyph (param $glyph i32) (param $x i32) (param $y i32) (param $size i32) (param $color i32)
    (local $mask i32)
    (local $row i32)
    (local $col i32)
    (local $bit_idx i32)

    i32.const 0
    local.set $mask

    local.get $glyph
    i32.const 0
    i32.eq
    if i32.const 0x7B6F local.set $mask end

    local.get $glyph
    i32.const 1
    i32.eq
    if i32.const 0x2492 local.set $mask end

    local.get $glyph
    i32.const 2
    i32.eq
    if i32.const 0x73E7 local.set $mask end

    local.get $glyph
    i32.const 3
    i32.eq
    if i32.const 0x73CF local.set $mask end

    local.get $glyph
    i32.const 4
    i32.eq
    if i32.const 0x5BC9 local.set $mask end

    local.get $glyph
    i32.const 5
    i32.eq
    if i32.const 0x79CF local.set $mask end

    local.get $glyph
    i32.const 6
    i32.eq
    if i32.const 0x79EF local.set $mask end

    local.get $glyph
    i32.const 7
    i32.eq
    if i32.const 0x7249 local.set $mask end

    local.get $glyph
    i32.const 8
    i32.eq
    if i32.const 0x7BEF local.set $mask end

    local.get $glyph
    i32.const 9
    i32.eq
    if i32.const 0x7BCF local.set $mask end

    local.get $glyph
    i32.const 10
    i32.eq
    if i32.const 0x7927 local.set $mask end ;; 'C'

    local.get $glyph
    i32.const 11
    i32.eq
    if i32.const 0x15D1 local.set $mask end ;; '<-'

    local.get $glyph
    i32.const 12
    i32.eq
    if i32.const 0x2E87 local.set $mask end ;; '+/-'

    local.get $glyph
    i32.const 13
    i32.eq
    if i32.const 0x21C2 local.set $mask end ;; '/'

    local.get $glyph
    i32.const 14
    i32.eq
    if i32.const 0x0AA8 local.set $mask end ;; '*'

    local.get $glyph
    i32.const 15
    i32.eq
    if i32.const 0x01C0 local.set $mask end ;; '-'

    local.get $glyph
    i32.const 16
    i32.eq
    if i32.const 0x05D0 local.set $mask end ;; '+'

    local.get $glyph
    i32.const 17
    i32.eq
    if i32.const 0x0E38 local.set $mask end ;; '='

    local.get $glyph
    i32.const 18
    i32.eq
    if i32.const 0x79E7 local.set $mask end ;; 'E'

    ;; Render 5 rows by 3 columns
    i32.const 0
    local.set $row
    (block $break_y
      (loop $loop_y
        local.get $row
        i32.const 5
        i32.ge_s
        br_if $break_y

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

            ;; Check if bit is set: (mask >> bit_idx) & 1
            local.get $mask
            local.get $bit_idx
            i32.shr_u
            i32.const 1
            i32.and
            if
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

  ;; =========================================================================
  ;; DISPLAY RENDERING (Right-aligned, multi-digit, negative support)
  ;; =========================================================================
  (func $draw_display_number (param $val i32)
    (local $is_neg i32)
    (local $abs_val i32)
    (local $digit_count i32)
    (local $rem i32)
    (local $cur_x i32)
    (local $cur_digit i32)
    (local $idx i32)

    ;; Error check
    global.get $g_has_error
    if
      i32.const 18 ;; GLYPH_E
      i32.const 200
      i32.const 27
      i32.const 4
      i32.const 0xC0392B ;; Red
      call $draw_glyph
      return
    end

    ;; Check negative
    local.get $val
    i32.const 0
    i32.lt_s
    if
      i32.const 1
      local.set $is_neg
      i32.const 0
      local.get $val
      i32.sub
      local.set $abs_val
    else
      i32.const 0
      local.set $is_neg
      local.get $val
      local.set $abs_val
    end

    ;; If zero
    local.get $abs_val
    i32.eqz
    if
      i32.const 0 ;; '0'
      i32.const 200
      i32.const 27
      i32.const 4
      i32.const 0x2C3E50
      call $draw_glyph
      return
    end

    ;; Extract digits into memory address 64..
    i32.const 0
    local.set $digit_count
    (block $break_digits
      (loop $loop_digits
        local.get $abs_val
        i32.eqz
        br_if $break_digits

        local.get $abs_val
        i32.const 10
        i32.rem_u
        local.set $rem

        local.get $abs_val
        i32.const 10
        i32.div_u
        local.set $abs_val

        i32.const 64
        local.get $digit_count
        i32.add
        local.get $rem
        i32.store8

        local.get $digit_count
        i32.const 1
        i32.add
        local.set $digit_count
        br $loop_digits
      )
    )

    ;; Draw digits right to left from cur_x = 200
    i32.const 200
    local.set $cur_x

    i32.const 0
    local.set $idx
    (block $break_draw
      (loop $loop_draw
        local.get $idx
        local.get $digit_count
        i32.ge_s
        br_if $break_draw

        i32.const 64
        local.get $idx
        i32.add
        i32.load8_u
        local.set $cur_digit

        local.get $cur_digit
        local.get $cur_x
        i32.const 27
        i32.const 4
        i32.const 0x2C3E50
        call $draw_glyph

        local.get $cur_x
        i32.const 16 ;; width 12 + 4 gap
        i32.sub
        local.set $cur_x

        local.get $idx
        i32.const 1
        i32.add
        local.set $idx
        br $loop_draw
      )
    )

    ;; If negative, draw '-' before first digit
    local.get $is_neg
    if
      i32.const 15 ;; GLYPH_MINUS
      local.get $cur_x
      i32.const 27
      i32.const 4
      i32.const 0x2C3E50
      call $draw_glyph
    end
  )

  ;; =========================================================================
  ;; OPERATOR INDICATOR: Shows active +, -, *, / in top-left of display
  ;; =========================================================================
  (func $draw_operator_indicator
    global.get $g_pending_op
    i32.const 1
    i32.eq
    if
      i32.const 16 ;; '+'
      i32.const 20
      i32.const 18
      i32.const 2
      i32.const 0x7F8C8D
      call $draw_glyph
    end

    global.get $g_pending_op
    i32.const 2
    i32.eq
    if
      i32.const 15 ;; '-'
      i32.const 20
      i32.const 18
      i32.const 2
      i32.const 0x7F8C8D
      call $draw_glyph
    end

    global.get $g_pending_op
    i32.const 3
    i32.eq
    if
      i32.const 14 ;; '*'
      i32.const 20
      i32.const 18
      i32.const 2
      i32.const 0x7F8C8D
      call $draw_glyph
    end

    global.get $g_pending_op
    i32.const 4
    i32.eq
    if
      i32.const 13 ;; '/'
      i32.const 20
      i32.const 18
      i32.const 2
      i32.const 0x7F8C8D
      call $draw_glyph
    end
  )

  ;; =========================================================================
  ;; BUTTON DRAW HELPER
  ;; =========================================================================
  (func $draw_button (param $id i32) (param $x i32) (param $y i32) (param $w i32) (param $h i32)
                     (param $normal_bg i32) (param $normal_fg i32) (param $hi_bg i32) (param $hi_fg i32)
    (local $bg i32)
    (local $fg i32)
    (local $glyph_x i32)
    (local $glyph_y i32)

    global.get $g_highlight_btn
    local.get $id
    i32.eq
    if
      local.get $hi_bg
      local.set $bg
      local.get $hi_fg
      local.set $fg
    else
      local.get $normal_bg
      local.set $bg
      local.get $normal_fg
      local.set $fg
    end

    ;; Draw button rectangle
    local.get $x
    local.get $y
    local.get $w
    local.get $h
    local.get $bg
    call $draw_rect

    ;; Center glyph (glyph size = 3: width = 9, height = 15)
    ;; glyph_x = x + (w - 9) / 2
    local.get $x
    local.get $w
    i32.const 9
    i32.sub
    i32.const 2
    i32.div_s
    i32.add
    local.set $glyph_x

    ;; glyph_y = y + (h - 15) / 2
    local.get $y
    local.get $h
    i32.const 15
    i32.sub
    i32.const 2
    i32.div_s
    i32.add
    local.set $glyph_y

    local.get $id
    local.get $glyph_x
    local.get $glyph_y
    i32.const 3
    local.get $fg
    call $draw_glyph
  )

  ;; =========================================================================
  ;; REDRAW FULL INTERFACE
  ;; =========================================================================
  (func $redraw_ui
    ;; 1. Window background
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    i32.const 0x242F3D ;; Dark slate
    call $draw_rect

    ;; Subtle frame border
    i32.const 0
    i32.const 0
    global.get $g_width
    i32.const 1
    i32.const 0x1B242F
    call $draw_rect

    ;; 2. Display box
    ;; Outer border
    i32.const 12
    i32.const 12
    i32.const 216
    i32.const 50
    i32.const 0x7F8C8D
    call $draw_rect

    ;; Interior
    i32.const 13
    i32.const 13
    i32.const 214
    i32.const 48
    i32.const 0xF4F6F7
    call $draw_rect

    ;; Display contents
    call $draw_operator_indicator
    global.get $g_display_value
    call $draw_display_number

    ;; 3. Buttons
    ;; Row 0: y=70, h=40
    ;; [ C ] at (12, 70, 48, 40) - Red
    i32.const 10
    i32.const 12
    i32.const 70
    i32.const 48
    i32.const 40
    i32.const 0xE74C3C
    i32.const 0xFFFFFF
    i32.const 0xC0392B
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ <- ] at (68, 70, 48, 40) - Gray
    i32.const 11
    i32.const 68
    i32.const 70
    i32.const 48
    i32.const 40
    i32.const 0x7F8C8D
    i32.const 0xFFFFFF
    i32.const 0x95A5A6
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ +/- ] at (124, 70, 48, 40) - Gray
    i32.const 12
    i32.const 124
    i32.const 70
    i32.const 48
    i32.const 40
    i32.const 0x7F8C8D
    i32.const 0xFFFFFF
    i32.const 0x95A5A6
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ / ] at (180, 70, 48, 40) - Orange
    i32.const 13
    i32.const 180
    i32.const 70
    i32.const 48
    i32.const 40
    i32.const 0xF39C12
    i32.const 0xFFFFFF
    i32.const 0xD35400
    i32.const 0xFFFFFF
    call $draw_button

    ;; Row 1: y=118, h=40
    ;; [ 7 ] at (12, 118, 48, 40)
    i32.const 7
    i32.const 12
    i32.const 118
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ 8 ] at (68, 118, 48, 40)
    i32.const 8
    i32.const 68
    i32.const 118
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ 9 ] at (124, 118, 48, 40)
    i32.const 9
    i32.const 124
    i32.const 118
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ * ] at (180, 118, 48, 40) - Orange
    i32.const 14
    i32.const 180
    i32.const 118
    i32.const 48
    i32.const 40
    i32.const 0xF39C12
    i32.const 0xFFFFFF
    i32.const 0xD35400
    i32.const 0xFFFFFF
    call $draw_button

    ;; Row 2: y=166, h=40
    ;; [ 4 ] at (12, 166, 48, 40)
    i32.const 4
    i32.const 12
    i32.const 166
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ 5 ] at (68, 166, 48, 40)
    i32.const 5
    i32.const 68
    i32.const 166
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ 6 ] at (124, 166, 48, 40)
    i32.const 6
    i32.const 124
    i32.const 166
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ - ] at (180, 166, 48, 40) - Orange
    i32.const 15
    i32.const 180
    i32.const 166
    i32.const 48
    i32.const 40
    i32.const 0xF39C12
    i32.const 0xFFFFFF
    i32.const 0xD35400
    i32.const 0xFFFFFF
    call $draw_button

    ;; Row 3: y=214, h=40
    ;; [ 1 ] at (12, 214, 48, 40)
    i32.const 1
    i32.const 12
    i32.const 214
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ 2 ] at (68, 214, 48, 40)
    i32.const 2
    i32.const 68
    i32.const 214
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ 3 ] at (124, 214, 48, 40)
    i32.const 3
    i32.const 124
    i32.const 214
    i32.const 48
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ + ] at (180, 214, 48, 40) - Orange
    i32.const 16
    i32.const 180
    i32.const 214
    i32.const 48
    i32.const 40
    i32.const 0xF39C12
    i32.const 0xFFFFFF
    i32.const 0xD35400
    i32.const 0xFFFFFF
    call $draw_button

    ;; Row 4: y=262, h=40
    ;; [ 0 ] (Wide 104x40) at (12, 262)
    i32.const 0
    i32.const 12
    i32.const 262
    i32.const 104
    i32.const 40
    i32.const 0xECF0F1
    i32.const 0x2C3E50
    i32.const 0x3498DB
    i32.const 0xFFFFFF
    call $draw_button

    ;; [ = ] (Wide 104x40) at (124, 262) - Green
    i32.const 17
    i32.const 124
    i32.const 262
    i32.const 104
    i32.const 40
    i32.const 0x27AE60
    i32.const 0xFFFFFF
    i32.const 0x2ECC71
    i32.const 0xFFFFFF
    call $draw_button

    ;; 4. Blit backbuffer to window
    global.get $g_window_id
    i32.const 65536
    global.get $g_width
    global.get $g_height
    call $blit_window
    drop
  )

  ;; =========================================================================
  ;; HIT TESTING: Finds button ID from local window coordinate (x, y)
  ;; =========================================================================
  (func $find_button_at (param $x i32) (param $y i32) (result i32)
    ;; Row 0: y in [70, 110)
    local.get $y
    i32.const 70
    i32.ge_s
    local.get $y
    i32.const 110
    i32.lt_s
    i32.and
    if
      local.get $x
      i32.const 12
      i32.ge_s
      local.get $x
      i32.const 60
      i32.lt_s
      i32.and
      if i32.const 10 return end ;; C

      local.get $x
      i32.const 68
      i32.ge_s
      local.get $x
      i32.const 116
      i32.lt_s
      i32.and
      if i32.const 11 return end ;; <-

      local.get $x
      i32.const 124
      i32.ge_s
      local.get $x
      i32.const 172
      i32.lt_s
      i32.and
      if i32.const 12 return end ;; +/-

      local.get $x
      i32.const 180
      i32.ge_s
      local.get $x
      i32.const 228
      i32.lt_s
      i32.and
      if i32.const 13 return end ;; /
    end

    ;; Row 1: y in [118, 158)
    local.get $y
    i32.const 118
    i32.ge_s
    local.get $y
    i32.const 158
    i32.lt_s
    i32.and
    if
      local.get $x
      i32.const 12
      i32.ge_s
      local.get $x
      i32.const 60
      i32.lt_s
      i32.and
      if i32.const 7 return end ;; 7

      local.get $x
      i32.const 68
      i32.ge_s
      local.get $x
      i32.const 116
      i32.lt_s
      i32.and
      if i32.const 8 return end ;; 8

      local.get $x
      i32.const 124
      i32.ge_s
      local.get $x
      i32.const 172
      i32.lt_s
      i32.and
      if i32.const 9 return end ;; 9

      local.get $x
      i32.const 180
      i32.ge_s
      local.get $x
      i32.const 228
      i32.lt_s
      i32.and
      if i32.const 14 return end ;; *
    end

    ;; Row 2: y in [166, 206)
    local.get $y
    i32.const 166
    i32.ge_s
    local.get $y
    i32.const 206
    i32.lt_s
    i32.and
    if
      local.get $x
      i32.const 12
      i32.ge_s
      local.get $x
      i32.const 60
      i32.lt_s
      i32.and
      if i32.const 4 return end ;; 4

      local.get $x
      i32.const 68
      i32.ge_s
      local.get $x
      i32.const 116
      i32.lt_s
      i32.and
      if i32.const 5 return end ;; 5

      local.get $x
      i32.const 124
      i32.ge_s
      local.get $x
      i32.const 172
      i32.lt_s
      i32.and
      if i32.const 6 return end ;; 6

      local.get $x
      i32.const 180
      i32.ge_s
      local.get $x
      i32.const 228
      i32.lt_s
      i32.and
      if i32.const 15 return end ;; -
    end

    ;; Row 3: y in [214, 254)
    local.get $y
    i32.const 214
    i32.ge_s
    local.get $y
    i32.const 254
    i32.lt_s
    i32.and
    if
      local.get $x
      i32.const 12
      i32.ge_s
      local.get $x
      i32.const 60
      i32.lt_s
      i32.and
      if i32.const 1 return end ;; 1

      local.get $x
      i32.const 68
      i32.ge_s
      local.get $x
      i32.const 116
      i32.lt_s
      i32.and
      if i32.const 2 return end ;; 2

      local.get $x
      i32.const 124
      i32.ge_s
      local.get $x
      i32.const 172
      i32.lt_s
      i32.and
      if i32.const 3 return end ;; 3

      local.get $x
      i32.const 180
      i32.ge_s
      local.get $x
      i32.const 228
      i32.lt_s
      i32.and
      if i32.const 16 return end ;; +
    end

    ;; Row 4: y in [262, 302)
    local.get $y
    i32.const 262
    i32.ge_s
    local.get $y
    i32.const 302
    i32.lt_s
    i32.and
    if
      local.get $x
      i32.const 12
      i32.ge_s
      local.get $x
      i32.const 116
      i32.lt_s
      i32.and
      if i32.const 0 return end ;; 0

      local.get $x
      i32.const 124
      i32.ge_s
      local.get $x
      i32.const 228
      i32.lt_s
      i32.and
      if i32.const 17 return end ;; =
    end

    i32.const -1
  )

  ;; =========================================================================
  ;; EXECUTE PENDING ARITHMETIC OPERATION
  ;; =========================================================================
  (func $execute_op (result i32)
    global.get $g_pending_op
    i32.const 1 ;; +
    i32.eq
    if
      global.get $g_accumulator
      global.get $g_display_value
      i32.add
      return
    end

    global.get $g_pending_op
    i32.const 2 ;; -
    i32.eq
    if
      global.get $g_accumulator
      global.get $g_display_value
      i32.sub
      return
    end

    global.get $g_pending_op
    i32.const 3 ;; *
    i32.eq
    if
      global.get $g_accumulator
      global.get $g_display_value
      i32.mul
      return
    end

    global.get $g_pending_op
    i32.const 4 ;; /
    i32.eq
    if
      global.get $g_display_value
      i32.eqz
      if
        i32.const 1
        global.set $g_has_error
        i32.const 0
        return
      end
      global.get $g_accumulator
      global.get $g_display_value
      i32.div_s
      return
    end

    global.get $g_display_value
  )

  ;; =========================================================================
  ;; BUTTON ACTION HANDLER
  ;; =========================================================================
  (func $handle_button_click (param $btn i32)
    (local $abs_d i32)

    ;; Digits 0..9
    local.get $btn
    i32.const 0
    i32.ge_s
    local.get $btn
    i32.const 9
    i32.le_s
    i32.and
    if
      global.get $g_has_error
      if
        i32.const 0
        global.set $g_has_error
        i32.const 0
        global.set $g_input_mode
      end

      global.get $g_input_mode
      i32.eqz
      if
        local.get $btn
        global.set $g_display_value
        i32.const 1
        global.set $g_input_mode
      else
        ;; Guard against overflow: abs(display) < 100,000,000
        global.get $g_display_value
        local.set $abs_d
        local.get $abs_d
        i32.const 0
        i32.lt_s
        if
          i32.const 0
          local.get $abs_d
          i32.sub
          local.set $abs_d
        end

        local.get $abs_d
        i32.const 100000000
        i32.lt_s
        if
          global.get $g_display_value
          i32.const 0
          i32.ge_s
          if
            global.get $g_display_value
            i32.const 10
            i32.mul
            local.get $btn
            i32.add
            global.set $g_display_value
          else
            global.get $g_display_value
            i32.const 10
            i32.mul
            local.get $btn
            i32.sub
            global.set $g_display_value
          end
        end
      end
      return
    end

    ;; Button 10: Clear 'C'
    local.get $btn
    i32.const 10
    i32.eq
    if
      i32.const 0
      global.set $g_display_value
      i32.const 0
      global.set $g_accumulator
      i32.const 0
      global.set $g_pending_op
      i32.const 0
      global.set $g_has_error
      i32.const 0
      global.set $g_input_mode
      return
    end

    ;; Button 11: Backspace '<-'
    local.get $btn
    i32.const 11
    i32.eq
    if
      global.get $g_has_error
      if
        i32.const 0
        global.set $g_has_error
        i32.const 0
        global.set $g_display_value
        i32.const 0
        global.set $g_input_mode
        return
      end

      global.get $g_input_mode
      if
        global.get $g_display_value
        i32.const 10
        i32.div_s
        global.set $g_display_value
        global.get $g_display_value
        i32.eqz
        if
          i32.const 0
          global.set $g_input_mode
        end
      end
      return
    end

    ;; Button 12: Negate '+/-'
    local.get $btn
    i32.const 12
    i32.eq
    if
      global.get $g_has_error
      if return end
      i32.const 0
      global.get $g_display_value
      i32.sub
      global.set $g_display_value
      return
    end

    ;; Operators: 13 (/), 14 (*), 15 (-), 16 (+)
    local.get $btn
    i32.const 13
    i32.ge_s
    local.get $btn
    i32.const 16
    i32.le_s
    i32.and
    if
      global.get $g_has_error
      if return end

      ;; If actively typing and already have a pending op, evaluate first
      global.get $g_input_mode
      if
        global.get $g_pending_op
        if
          call $execute_op
          global.set $g_display_value
        end
      end

      global.get $g_has_error
      if return end

      global.get $g_display_value
      global.set $g_accumulator

      ;; Assign pending_op: 16(+)->1, 15(-)->2, 14(*)->3, 13(/)->4
      local.get $btn
      i32.const 16
      i32.eq
      if i32.const 1 global.set $g_pending_op end

      local.get $btn
      i32.const 15
      i32.eq
      if i32.const 2 global.set $g_pending_op end

      local.get $btn
      i32.const 14
      i32.eq
      if i32.const 3 global.set $g_pending_op end

      local.get $btn
      i32.const 13
      i32.eq
      if i32.const 4 global.set $g_pending_op end

      i32.const 0
      global.set $g_input_mode
      return
    end

    ;; Button 17: Equals '='
    local.get $btn
    i32.const 17
    i32.eq
    if
      global.get $g_has_error
      if return end

      global.get $g_pending_op
      if
        call $execute_op
        global.set $g_display_value
        global.get $g_display_value
        global.set $g_accumulator
        i32.const 0
        global.set $g_pending_op
        i32.const 0
        global.set $g_input_mode
      end
      return
    end
  )

  ;; =========================================================================
  ;; MAIN ENTRY POINT (_start)
  ;; =========================================================================
  (func (export "_start")
    (local $event_popped i32)
    (local $ev_type i32)
    (local $ev_x i32)
    (local $ev_y i32)
    (local $click_btn i32)
    (local $delay i32)

    ;; 1. Register calculator window
    global.get $g_width
    global.get $g_height
    i32.const 200
    i32.const 0
    call $create_window_ex
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
      (block $break_events
        (loop $loop_events
          ;; Poll next IPC message (struct resides at address 32)
          i32.const 32
          call $ipc_recv
          local.set $event_popped

          local.get $event_popped
          i32.eqz
          br_if $break_events

          ;; Load message fields from struct at address 32:
          ;; offset 8 (addr 40): type
          ;; offset 12 (addr 44): arg1 (local x)
          ;; offset 16 (addr 48): arg2 (local y)
          i32.const 40
          i32.load
          local.set $ev_type

          i32.const 44
          i32.load
          local.set $ev_x

          i32.const 48
          i32.load
          local.set $ev_y

          ;; If type == MSG_CLOSE (3)
          local.get $ev_type
          i32.const 3
          i32.eq
          if
            i32.const 0
            call $proc_exit
          end

          ;; If type == MSG_MOUSE_CLICK (1)
          local.get $ev_type
          i32.const 1
          i32.eq
          if
            local.get $ev_x
            local.get $ev_y
            call $find_button_at
            local.set $click_btn

            local.get $click_btn
            i32.const -1
            i32.ne
            if
              ;; Set highlight
              local.get $click_btn
              global.set $g_highlight_btn

              ;; Process action
              local.get $click_btn
              call $handle_button_click

              ;; Redraw with highlight and updated state
              call $redraw_ui

              ;; Visible feedback delay (~30-40ms loop)
              i32.const 600000
              local.set $delay
              (loop $delay_loop
                local.get $delay
                i32.const 1
                i32.sub
                local.set $delay
                local.get $delay
                i32.const 0
                i32.gt_s
                br_if $delay_loop
              )

              ;; Clear highlight and redraw
              i32.const -1
              global.set $g_highlight_btn
              call $redraw_ui
            end
          end

          br $loop_events
        )
      )

      ;; Wait for next IPC message (blocks thread until message arrives)
      call $wait_message
      drop

      br $event_loop
    )
  )
)