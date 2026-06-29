(module
  ;; Импортируем сетевые функции из пространства env
  (import "env" "woos_socket_create" (func $socket_create (param i32) (result i32)))
  (import "env" "woos_socket_bind" (func $socket_bind (param i32 i32) (result i32)))
  (import "env" "woos_socket_listen" (func $socket_listen (param i32) (result i32)))
  (import "env" "woos_socket_accept" (func $socket_accept (param i32) (result i32)))
  (import "env" "woos_socket_send" (func $socket_send (param i32 i32 i32) (result i32)))
  (import "env" "woos_socket_recv" (func $socket_recv (param i32 i32 i32) (result i32)))
  (import "env" "woos_socket_close" (func $socket_close (param i32) (result i32)))

  ;; Импортируем стандартный proc_exit из WASI
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))

  (memory (export "memory") 1)

  ;; Строка HTTP-ответа (хранится по адресу 1024)
  (data (i32.const 1024) "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 38\r\nConnection: close\r\n\r\nHello from WebAssembly on WoOS Kernel!\n")

  ;; Буфер приема (по адресу 2048)
  (global $rx_buf i32 (i32.const 2048))

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
        ;; Читаем HTTP-запрос в rx_buf
        (call $socket_recv (local.get $client) (global.get $rx_buf) (i32.const 512))
        drop
        
        ;; Отправляем подготовленный HTTP-ответ (смещение 1024, длина 122)
        (call $socket_send (local.get $client) (i32.const 1024) (i32.const 122))
        drop
        
        ;; Закрываем клиентское соединение
        (call $socket_close (local.get $client))
        drop
      end
      
      ;; Зацикливаемся
      br $accept_loop
    )
  )
)
