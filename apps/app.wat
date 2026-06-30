(module
  ;; Imports from env
  (import "env" "woos_graphics_get_info" (func $get_info (param i32) (result i32)))
  (import "env" "woos_graphics_present_rect" (func $present_rect (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "woos_input_poll_event" (func $poll_event (param i32) (result i32)))

  ;; Import standard WASI functions
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))

  ;; Export linear memory: 90 pages (5.76 MB)
  (memory (export "memory") 90)

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

    ;; Clip x
    local.get $x
    i32.const 0
    i32.lt_s
    if
      i32.const 0
      local.set $x
    end

    local.get $x_end
    global.get $g_width
    i32.gt_s
    if
      global.get $g_width
      local.set $x_end
    end

    ;; Clip y
    local.get $y
    i32.const 0
    i32.lt_s
    if
      i32.const 0
      local.set $y
    end

    local.get $y_end
    global.get $g_height
    i32.gt_s
    if
      global.get $g_height
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

  ;; Save 12x12 pixels under cx, cy to address 100
  (func $save_cursor_underlay (param $cx i32) (param $cy i32)
    (local $py i32)
    (local $px i32)
    (local $saved_ptr i32)
    (local $pixel_ptr i32)
    (local $row_offset i32)
    (local $tx i32)
    (local $ty i32)

    i32.const 100
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

  ;; Restore 12x12 pixels under cx, cy from address 100
  (func $restore_cursor_underlay (param $cx i32) (param $cy i32)
    (local $py i32)
    (local $px i32)
    (local $saved_ptr i32)
    (local $pixel_ptr i32)
    (local $row_offset i32)
    (local $tx i32)
    (local $ty i32)

    i32.const 100
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

  ;; Draw cursor cross at cx, cy
  (func $draw_cursor (param $cx i32) (param $cy i32) (param $color i32)
    ;; Horizontal line: cx - 6, cy - 1, 12, 2
    local.get $cx
    i32.const 6
    i32.sub
    local.get $cy
    i32.const 1
    i32.sub
    i32.const 12
    i32.const 2
    local.get $color
    call $draw_rect

    ;; Vertical line: cx - 1, cy - 6, 2, 12
    local.get $cx
    i32.const 1
    i32.sub
    local.get $cy
    i32.const 6
    i32.sub
    i32.const 2
    i32.const 12
    local.get $color
    call $draw_rect
  )

  ;; Entry point _start
  (func (export "_start")
    (local $event_popped i32)
    (local $ev_type i32)
    (local $ev_x i32)
    (local $ev_y i32)

    ;; 1. Get Video parameters
    i32.const 0
    call $get_info
    drop

    ;; Load into globals
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

    ;; 2. Initialize screen buffer (Desktop color: 0x2C3E50)
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    i32.const 0x2C3E50
    call $draw_rect

    ;; Draw a window body (light gray 0xECF0F1)
    i32.const 250
    i32.const 180
    i32.const 450
    i32.const 320
    i32.const 0xECF0F1
    call $draw_rect

    ;; Draw a window header (beautiful blue 0x2980B9)
    i32.const 250
    i32.const 180
    i32.const 450
    i32.const 35
    i32.const 0x2980B9
    call $draw_rect

    ;; Draw a window close button (red 0xE74C3C)
    i32.const 665
    i32.const 190
    i32.const 20
    i32.const 15
    i32.const 0xE74C3C
    call $draw_rect

    ;; Draw an accent line on window border (0xBDC3C7)
    i32.const 250
    i32.const 215
    i32.const 450
    i32.const 3
    i32.const 0xBDC3C7
    call $draw_rect

    ;; Initial cursor positions
    i32.const 640
    global.set $g_cursor_x
    i32.const 512
    global.set $g_cursor_y
    i32.const 640
    global.set $g_old_x
    i32.const 512
    global.set $g_old_y

    ;; Save first underlay
    global.get $g_cursor_x
    global.get $g_cursor_y
    call $save_cursor_underlay

    ;; Draw first cursor
    global.get $g_cursor_x
    global.get $g_cursor_y
    i32.const 0xE74C3C ;; Vibrant Red cursor
    call $draw_cursor

    ;; Present full screen initially
    i32.const 65536
    i32.const 0
    i32.const 0
    global.get $g_width
    global.get $g_height
    call $present_rect
    drop

    ;; 3. Main event loop
    (loop $event_loop
      ;; Poll next input event (struct resides at address 32)
      i32.const 32
      call $poll_event
      local.set $event_popped

      local.get $event_popped
      if
        ;; Load event fields
        ;; type is at address 32
        i32.const 32
        i32.load
        local.set $ev_type

        ;; x is at address 36 (uint16)
        i32.const 36
        i32.load16_u
        local.set $ev_x

        ;; y is at address 38 (uint16)
        i32.const 38
        i32.load16_u
        local.set $ev_y

        ;; If type == INPUT_EVENT_MOUSE_MOVE (0)
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

          ;; Erase old cursor
          global.get $g_old_x
          global.get $g_old_y
          call $restore_cursor_underlay

          ;; Save new underlay
          global.get $g_cursor_x
          global.get $g_cursor_y
          call $save_cursor_underlay

          ;; Draw new cursor
          global.get $g_cursor_x
          global.get $g_cursor_y
          i32.const 0xE74C3C ;; Red
          call $draw_cursor

          ;; Present the old cursor dirty region (12x12)
          i32.const 65536
          global.get $g_old_x
          i32.const 6
          i32.sub
          global.get $g_old_y
          i32.const 6
          i32.sub
          i32.const 12
          i32.const 12
          call $present_rect
          drop

          ;; Present the new cursor dirty region (12x12)
          i32.const 65536
          global.get $g_cursor_x
          i32.const 6
          i32.sub
          global.get $g_cursor_y
          i32.const 6
          i32.sub
          i32.const 12
          i32.const 12
          call $present_rect
          drop
        end
      end

      br $event_loop
    )
  )
)
