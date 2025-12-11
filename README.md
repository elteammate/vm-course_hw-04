# Собрать

```shell
cmake --build cmake-build-debug --target hw
```

# Запустить

```shell
./cmake-build-debug/hw 00-smoke.bc
```

Чтобы запустить с измерением производительности

```shell
./cmake-build-debug/hw 00-smoke.bc
```

# Запустить тесты

```shell
python scripts/test.py
```

# Производительность

Не такой большой прирост, как хотелось бы,
видимо потому что глубина стека - это лишь одна проверка из многих.

Статзначимость измерять не буду.

```shell
$ time echo "0" | lamac -i Lama/performance/Sort.lama
________________________________________________________
Executed in  313.76 secs    fish           external
   usr time  311.73 secs   48.00 micros  311.72 secs
   sys time    2.02 secs   62.00 micros    2.02 secs

$ time echo "0" | lamac -s Lama/performance/Sort.lama
________________________________________________________
Executed in   89.75 secs    fish           external
   usr time   88.11 secs    0.00 micros   88.11 secs
   sys time    1.63 secs  149.00 micros    1.63 secs

$ lamac -b Lama/performance/Sort.lama && time ../hw-02/cmake-build-debug/vm_course_02 Sort.bc
________________________________________________________
Executed in   91.30 secs    fish           external
   usr time   87.46 secs  102.00 micros   87.46 secs
   sys time    3.83 secs  129.00 micros    3.83 secs

$  lamac -b Lama/performance/Sort.lama && time ./cmake-build-debug/hw profile Sort.bc
Analysis time: 0.017770ms
Execution time: 90.423000s

________________________________________________________
Executed in   90.79 secs    fish           external
   usr time   86.86 secs  261.00 micros   86.86 secs
   sys time    3.93 secs   97.00 micros    3.93 secs
```
