Zbiór programów zawartych w tej paczce pozwala na monitorowanie systemu trunkingowego EDACS96. Konieczne do tego są dwa odbiorniki RTL-SDR. Możliwe jest użycie jednego odbiornika, jednak taka funkcjonalność nie została zaimplementowana w prezentowanych programach.

**Przed przystąpieniem do działań, należy obowiązkowo zapoznać się z poniższą instrukcją operacyjną.**

UWAGA! Do poprawnej pracy wymagane jest zainstalowanie pakietu rtl_sdr JAK RÓWNIEŻ programu rtl_udp, który jest forkiem rtl_fm. Umożliwia on m.in. przestrajanie odbiornika poprzez wysyłanie do programu pakietów UDP. Testowane na Debianie Jessie 32-bit.

1. Należy utworzyć katalog /edacs/systems
2. W folderze utworzyć plik tekstowy bez rozszerzenia i umieścić w nim częstotliwości LCNów, kolejno LCN1, LCN2... linia po linii, wyrażone w hercach. Dla wygody nazwa pliku powinna opisywać system. Nie tworzyć pustej linii na końcu!
3. Przejść do folderu z programem detector i użyć komendy:

rtl_udp -d 1 -f 460M -s 28.8k -p 73 -l 5000 -g 45 | tee >(sox -t raw -b 16 -e signed-integer -r 28800 -c 1 - -t raw - vol 2 sinc 0.2k-4.5k -a 110 rate 14400 | aplay -t raw -f S16_LE -r 14400 -c 1) | ./detector

-d 1     - urządzenie 1
-f 460M  - częstotliwość odbiorcza, nie ma większego znaczenia
-s 28.8k - częstotliwość próbkowania - NIE DOTYKAĆ
-p 73    - korekcja częstoliwości w ppm
-l 5000  - początkowa blokada szumu - duża wartość
-g 45    - wzmocnienie sygnału wejściowego, w przypadku użycia automatycznej regulacji wzmocnienia, nie stosować tej opcji

sox, aplay - nie dotykać
sinc 0.2-4.5k -a 110 tworzy filtr pasmowoprzepustowy o stromym zboczu. Ma na celu usunąć dźwięk sygnalizacji końca transmisji głosowej na kanale rozmównym (4800Hz).
Po uruchomieniu powinien zostać utworzony plik 'squelch' w katalogu /tmp.

4. Przejść do folderu z programem 'decoder'

rtl_fm -d 0 -f 460.125M -s 28.8k -p 7 | ./decoder psy 1 4 4

-d 0        - urządzenie 0
-f 460.125M - częstotliwość kanału kontrolnego, należy wpisać odpowiednią wartość
-s 28.8k    - częstotliwość próbkowania - NIE DOTYKAĆ
-p 7        - korekcja częstoliwości w ppm

Opcje programu 'decoder':
./decoder nazwa_pliku cc a f [A F S]
nazwa_pliku - nazwa pliku z folderu /edacs/systems, załóżmy 'smutni_panowie'
cc          - numer kanału kontrolnego
a f         - ile bitów w ramce AFS zajmuje numer AGENCY i FLEET (domyślnie 4 4)
[A F S]     - opcjonalny filtr, przepuszczanie tylko jednej grupy. Przykładowo zapis 8 17 6 oznacza grupę 08-176
Komenda wygląda wtedy tak: ./decoder smutni_panowie 1 4 4 8 17 6

JESZCZE NIE DZIAŁA: 5. Dodatkowo program 'cc_finder' umożliwia odnalezienie częstotliwości kanału kontrolnego. Wywołanie: rtl_udp -d 0 -f 460M -s 28.8k -p 7 | ./cc_search nazwa_pliku
Częstotliwość podawana przez opcję -f nie ma to znaczenia. Program pobiera listę LCNów z pliku i zwraca LCN kanału kontrolnego.