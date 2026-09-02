# c-mini-dig 

**Эта утилита пародирует известную утилиту "dig"** 

Работа над mini-dig временно приостановлена потому что что автору нужно делать смертельно скучные предметы в универе.

**Это упрощённая версия её функционал очень ограничен относительно утилиты dig.** 

## Компиляция и запуск утилиты 

### Компиляция: 
`gcc mini-dig.c -o mini-dig`. 

### Запуск: 
Запуск выполняется в таком формате: 
`./mini-dig <server> <domain> <type>`. 
1. `<server>` - IPv4 адрес DNS-сервера, которому будет отправлен запрос. Должен начинаться с символа '@'. Поставьте дифис чтобы был выбран dns-сервер системы (будет прочитан первый из файла `/etc/resolv.conf`). Фанаты утилиты resolvectl скорее всего имеют в ОС кэширующий локальный резолвер systemd-resolved, в файле `/etc/resolv.conf` будет стоять адрес 127.0.0.53 и будет использоваться утилитой в случае выбора dns-сервера системы. Узнать адрес резолвера, которому systemd-resolved перенаправляет запросы можно с помощью команды `resolvectl status`. 
2. `<domain>` - domain, о котором нужно найти информацию. Поставьте дефис чтобы был выбран домен example.com. 
3. `<type>` - тип запроса. **Поддерживаемые типы запроса: A, AAAA, NS, CNAME, SOA.** Поставьте дефис чтобы был выбран A. 

#### Пример запуска: 
`./mini-dig @1.1.1.1 example.com A` 
или 
`./mini-dig - - -` 


# Пример запуска и работы программы: 

```
trigger@transistor1:~/Documents/prog/c/netprog/mini-dig$ ./mini-dig @192.168.0.1 google.com NS

Сокет создан успешно. Номер сокета: 4

Собираю DNS сообщение: Transaction ID = 0x9b0c, flags = 0x0100, qdcount = 0x0001, ancount = 0x0000, nscount = 0x0000, arcount = 0x0000. Длина сформированного DNS-запроса: 28

Generated send buffer: 
0000: 9b 0c 01 00 00 01 00 00 00 00 
000a: 00 00 06 67 6f 6f 67 6c 65 03 
0014: 63 6f 6d 00 00 02 00 01       

DNS-сообщение отправлено успешно. Отправлено: 28 байт

DNS-ответ получен: 276 байт
Отправитель (IPv4): 192.168.0.1. Порт отправителя: 53

Received buffer: 
0000: 9b 0c 81 80 00 01 00 04 00 00 
000a: 00 08 06 67 6f 6f 67 6c 65 03 
0014: 63 6f 6d 00 00 02 00 01 c0 0c 
001e: 00 02 00 01 00 00 f7 aa 00 06 
0028: 03 6e 73 33 c0 0c c0 0c 00 02 
0032: 00 01 00 00 f7 aa 00 06 03 6e 
003c: 73 31 c0 0c c0 0c 00 02 00 01 
0046: 00 00 f7 aa 00 06 03 6e 73 34 
0050: c0 0c c0 0c 00 02 00 01 00 00 
005a: f7 aa 00 06 03 6e 73 32 c0 0c 
0064: c0 4c 00 01 00 01 00 00 f7 aa 
006e: 00 04 d8 ef 26 0a c0 4c 00 1c 
0078: 00 01 00 00 f7 aa 00 10 20 01 
0082: 48 60 48 02 00 38 00 00 00 00 
008c: 00 00 00 0a c0 5e 00 01 00 01 
0096: 00 00 f7 aa 00 04 d8 ef 22 0a 
00a0: c0 5e 00 1c 00 01 00 00 f7 aa 
00aa: 00 10 20 01 48 60 48 02 00 34 
00b4: 00 00 00 00 00 00 00 0a c0 28 
00be: 00 01 00 01 00 00 f7 aa 00 04 
00c8: d8 ef 24 0a c0 28 00 1c 00 01 
00d2: 00 00 f7 aa 00 10 20 01 48 60 
00dc: 48 02 00 36 00 00 00 00 00 00 
00e6: 00 0a c0 3a 00 01 00 01 00 00 
00f0: f7 ba 00 04 d8 ef 20 0a c0 3a 
00fa: 00 1c 00 01 00 00 f7 39 00 10 
0104: 20 01 48 60 48 02 00 32 00 00 
010e: 00 00 00 00 00 0a             

Transaction ID = 0x9b0c 

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

Количество resource records в секциях: QDCOUNT = 0x0001, ANCOUNT = 0x0004, NSCOUNT = 0x0000, ARCOUNT = 0x0008. 


Секция Question: 
Name                           Type   Class 
google.com                     NS     1     

Секция Answer: 
Name                           Type   Class  TTL        RDLENGTH   RDATA                                  
google.com                     NS     1      63402      6          ns3.google.com                         
google.com                     NS     1      63402      6          ns1.google.com                         
google.com                     NS     1      63402      6          ns4.google.com                         
google.com                     NS     1      63402      6          ns2.google.com                         

Записей в секции Authority нет

Секция Additional Information: 
Name                           Type   Class  TTL        RDLENGTH   RDATA                                  
ns4.google.com                 A      1      63402      4          216.239.38.10                          
ns4.google.com                 AAAA   1      63402      16         2001:4860:4802:38::a                   
ns2.google.com                 A      1      63402      4          216.239.34.10                          
ns2.google.com                 AAAA   1      63402      16         2001:4860:4802:34::a                   
ns3.google.com                 A      1      63402      4          216.239.36.10                          
ns3.google.com                 AAAA   1      63402      16         2001:4860:4802:36::a                   
ns1.google.com                 A      1      63418      4          216.239.32.10                          
ns1.google.com                 AAAA   1      63289      16         2001:4860:4802:32::a
```
