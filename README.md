# Настроить

```shell
mkdir cmake-build
cd cmake-build
cmake ..
cd ..
```

# Собрать

```shell
cmake --build cmake-build --target hw
```

# Запустить

```shell
./cmake-build/hw 00-smoke.bc
```

Чтобы запустить с измерением производительности

```shell
./cmake-build/hw 00-smoke.bc
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

$ lamac -b Lama/performance/Sort.lama && time ../hw-02/cmake-build/vm_course_02 Sort.bc
________________________________________________________
Executed in   91.30 secs    fish           external
   usr time   87.46 secs  102.00 micros   87.46 secs
   sys time    3.83 secs  129.00 micros    3.83 secs

$  lamac -b Lama/performance/Sort.lama && time ./cmake-build/hw profile Sort.bc
Analysis time: 0.013255ms
Execution time: 89.722000s

________________________________________________________
Executed in   90.03 secs    fish           external
   usr time   86.20 secs  241.00 micros   86.20 secs
   sys time    3.83 secs  128.00 micros    3.83 secs
```

# Список проверок

- `[static]` Строковые литералы есть в таблице строк и являются корректными C-строками.
- `[static]` Достижимые переходы попадают в границы кода
- `[static]` Достижимый код состоит их корретных инструкций
- `[static]` ip не переполняется
- `[static]` Все статические объекты индексируются `i32`
- `[static]` Количество локалов, аргументов, временных значений, и захваченных переменных функции не превосходит `0xFFFF`.
- `[static]` При сливании потоков управления глубина стека одинакова.
- `[static]` Доступы к любым переменным корректны
- `[static]` Копии замыканий создаются с одинаковым количеством захваченных переменных
- `[static]` Стек не underflows
- `[static]` Все функции начинаются с `[C]BEGIN`
- `[static]` Поток управления не из вызова функции не может дойти до `[C]BEGIN`
- `[static]` Замыкания создаются из корректных функций
- `[static]` `CALL` не вызывает замыканий
- `[static]` Секция кода заканчивается на байт `0xFF`
- `[dynamic]` Стеки не переполняются (один раз, при вызове)
- `[dymanic]` Арифметические операции работают с числами (каждую операцию)
- `[dynamic]` Нет деления на 0
