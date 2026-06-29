(module
  ;; Импортируем сетевые функции управления сокетами из пространства env
  (import "env" "woos_socket_create" (func $socket_create (param i32) (result i32)))
  (import "env" "woos_socket_bind" (func $socket_bind (param i32 i32) (result i32)))
  (import "env" "woos_socket_listen" (func $socket_listen (param i32) (result i32)))
  (import "env" "woos_socket_accept" (func $socket_accept (param i32) (result i32)))

  ;; Импортируем стандартные WASI-функции ввода-вывода и закрытия файлов
  (import "wasi_snapshot_preview1" "fd_read" (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $fd_close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))

  (memory (export "memory") 1)

  ;; Строка HTTP-ответа (хранится по адресу 1024)
  (data (i32.const 1024) "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 38\r\nConnection: close\r\n\r\nHello from WebAssembly on WoOS Kernel!\n")

  ;; Буфер приема (по адресу 2048)
  (global $rx_buf i32 (i32.const 2048))

  ;; Буфер для структуры iovec (по адресу 4096)
  ;; iovec.buf: 4096 (4 байта)
  ;; iovec.buf_len: 4100 (4 байта)
  (global $iovec i32 (i32.const 4096))
  ;; Буфер для возвращаемого размера (по адресу 4128)
  (global $io_res i32 (i32.const 4128))

  (func (export "_start")
    (local $server i32)
    (local $client i32)
    
    ;; Создаем TCP сокет (type=1)
    (call $socket_create (i32.const 1))
    local.set $server
    
    ;; Если дескриптор отрицательный, выходим
    local.get $server
    i32.const -1
    i32.eq
    if
      (call $proc_exit (i32.const 1))
    end
    
    ;; Привязываем к порту 80
    (call $socket_bind (local.get $server) (i32.const 80))
    drop
    
    ;; Начинаем слушать подключения
    (call $socket_listen (local.get $server))
    drop
    
    ;; Бесконечный цикл приема входящих соединений
    (loop $accept_loop
      (call $socket_accept (local.get $server))
      local.set $client
      
      local.get $client
      i32.const 0
      i32.ge_s
      if
        ;; 1. Читаем запрос из сокета через WASI fd_read
        ;; Настраиваем iovec на rx_buf (адрес 2048, длина 512)
        (i32.store (global.get $iovec) (global.get $rx_buf))
        (i32.store (i32.add (global.get $iovec) (i32.const 4)) (i32.const 512))
        (call $fd_read (local.get $client) (global.get $iovec) (i32.const 1) (global.get $io_res))
        drop
        
        ;; 2. Отправляем ответ в сокет через WASI fd_write
        ;; Настраиваем iovec на HTTP-ответ (адрес 1024, длина 122)
        (i32.store (global.get $iovec) (i32.const 1024))
        (i32.store (i32.add (global.get $iovec) (i32.const 4)) (i32.const 122))
        (call $fd_write (local.get $client) (global.get $iovec) (i32.const 1) (global.get $io_res))
        drop
        
        ;; 3. Закрываем клиентский сокет через WASI fd_close
        (call $fd_close (local.get $client))
        drop
      end
      
      ;; Зацикливаемся
      br $accept_loop
    )
  )
)
