## SOP 2 Lab - task 1
**FIFO/PIPE**

Napisz program używający łączy pipe do jednostronnej komunikacji pomiędzy trzema procesami. Każdy proces jest połączony z każdym innym jednym łączem pipe. Procesy tworzą coś w rodzaju trójkąta z jednym wyróżnionym rogiem (proces rodzic), kierunek łącza ma być tak dobrany aby możliwe było przesłanie komunikatów „w koło” pomiędzy procesami.
Początkowo proces rodzic ma wysłać liczbę 1 (jako tekst o zmiennej długości) w obieg, potem procesy pracują już identycznie tzn. odbierają liczbę, wypisują ją na stdout wraz ze swoim PID, zmieniają ją o losowy czynnik [-10,10] i przesyłają dalej. Jeśli któryś z procesów odbierze liczbę 0 to ma się zakończyć. Inne procesy poprzez detekcję zerwanego łącza także mają się zakończyć.
