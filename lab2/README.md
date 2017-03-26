## SOP 2 Lab - task 2
**Posix queues**

Program składa się z jednego procesu i kolejki przez niego obsługiwanej. Kolejka ma nazwę PID_queue i ma być usuwana podczas zakończenia programu. Program czyta wiadomości z kolejki, należy obsłużyć następujące typy wiadomości z zachowaniem priorytetu (3 najwyższy priorytet, 1 najniższy):

1. Rejestracja do sieci, inny proces podaje swój PID, program ma go zapamiętać (pamiętamy do 5 peerów w sumie), proces wysyłający żądanie rejestracji sam oczywiście dodaje PID, u którego się rejestruje do swojej listy peerów.

2. Przesyłka wiadomości (do 20 znaków), jeśli wiadomość jest adresowana do procesu to ją wyświetlamy, jeśli jest adresowana do innego znanego już peera, to przesyłamy ją tylko do kolejki tego procesu. Jeśli odbiorca jest nieznany, to przesyłamy ją wszystkim znanym nam peerom poza nadawcą.

3. Zakończenie działania programu, dodatkowo wymaga wysłania żądania zakończenia do wszystkich znanych peerów.

Dodatkowo program czyta linie tekstu z sdtin, format ma być postaci PID tekst i należy go traktować tak samo jak komunikat o priorytecie 2. Jeśli program otrzyma SIGINT, to postępuje tak samo jak w przypadku wiadomości o priorytecie 3. Jeśli program podczas startu otrzyma parametr, to należy go potraktować jak PID programu, do którego należy się zarejestrować, aby podłączyć się do sieci p2p.

Jeśli w trakcie komunikacji nie uda się przesłać danych przez kolejkę jakiegoś procesu, należy zaniechać danej wysyłki. Resztę działań wykonujemy, tak jakby błędu nie było.
