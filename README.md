# c-mini-dig 

**Эта утилита пародирует известную утилиту "dig"** 

**Программа еще в процессе разработки и её функционал очень ограничен.** 

## Компиляция и запуск утилиты 

### Компиляция: 
`gcc mini-dig.c -o mini-dig` 

### Запуск: 
Запуск выполняется в таком формате: 
`./mini-dig <server> <domain> <type>.` 
1. `<server>` - IPv4 адрес DNS-сервера, которому будет отправлен запрос. Должен начинаться с символа '@'. Поставьте дифис чтобы был выбран dns-сервер системы (будет прочитан первый из файла /etc/resolv.conf).
2. `<domain>` - domain, о котором нужно найти информацию. Поставьте дефис чтобы был выбран домен example.com 
3. `<type>` - тип запроса: А - запрос IPv4 адреса для domain или АААА - запрос IPv6 адреса для domain. Поставьте дефис чтобы был выбран A. _Пока доступны только типы А и АААА_ 

#### Пример запуска: 
`./mini-dig @1.1.1.1 example.com A` 


# Пример запуска и работы программы: 

```
trigger@transistor1:~/Documents/prog/c/netprog/mini-dig$ ./mini-dig @1.1.1.1 example.com A

Сокет создан успешно. Номер сокета: 10

Собираю DNS сообщение: Transaction ID = 0x37ed, flags = 0x0100, qdcount = 0x0001, ancount = 0x0000, nscount = 0x0000, arcount = 0x0000. Длина сформированного DNS-запроса: 29

Generated send buffer: 
0000: 37 ed 01 00 00 01 00 00 00 00 
000a: 00 00 07 65 78 61 6d 70 6c 65 
0014: 03 63 6f 6d 00 00 01 00 01    

DNS-сообщение отправлено успешно. Отправлено: 29 байт

DNS-ответ получен: 61 байт
Отправитель (IPv4): 1.1.1.1. Порт отправителя: 53

Received buffer: 
0000: 37 ed 81 80 00 01 00 02 00 00 
000a: 00 00 07 65 78 61 6d 70 6c 65 
0014: 03 63 6f 6d 00 00 01 00 01 c0 
001e: 0c 00 01 00 01 00 00 00 ae 00 
0028: 04 08 2f 45 00 c0 0c 00 01 00 
0032: 01 00 00 00 ae 00 04 08 06 70 
003c: 00                            

Transaction ID = 0x37ed 

Flags = 0x8180: 
QR = 1 
OpCode = 0000 
AA = 0 
TC = 0 
RD = 1 
RA = 1 
Z  = 0 
AD = 0 
CD = 0 
RCODE = 0000
RCODE = 0: No error.

Количество resource records в секциях: QDCOUNT = 0x0001, ANCOUNT = 0x0002, NSCOUNT = 0x0000, ARCOUNT = 0x0000. 


Секция Question: 
Name                           Type   Class 
example.com                    A      1     

Секция Answer: 
Name                           Type   Class  TTL        RDLENGTH   RDATA                                  
example.com                    A      1      174        4          8.47.69.0                              
example.com                    A      1      174        4          8.6.112.0                              

Записей в секции Authority нет

Записей в секции Additional Information нет

```
