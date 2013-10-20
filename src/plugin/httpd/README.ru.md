# Сервер http

В данный плагин включены следующие вещи

1. сервер http (частично HTTP/1.1)
1. клиент http (пока без SSL)

## Клиент http

Позволяет выполнять http запросы

### box.http.request(method, url, headers, body)

Выполняет http-запрос на указанный урл (`url`).
В качестве метода `method` может быть передано значение `GET` или `POST`.

Возвращает луа-табличку со следующими полями:

* `status` - статус ответа удаленного HTTP-сервера
* `reason` - текстовый статус ответа удаленного HTTP-сервера
* `headers` - табличка с нормализованными заголовками ответа
* `body` - тело ответа
* `proto` - версия протокола

На настоящее время поддерживается только протокол http (https в планах)

#### Примеры

```lua

    r = box.http.request('GET', 'http://google.com')
    r = box.http.request('POST', 'http://google.com', {}, 'text=123')

```
## Сервер http

Сервер можно инициализировать на произвольном порту к которому у тарантула
имеется доступ на `bind` (если тарантул запущен не как `root`, то
привилегированные порты могут быть недоступны).

Запуск сервера производится с использованием следующих стадий:

* Создание сервера (`httpd = box.httpd.new`)
* Конфигурирование роутинга (`httpd:route(...)`)
* Собственно запуск сервера (`httpd:start()`)
* Останов сервера (`httpd:stop()`)

### Создание сервера `box.httpd.new`

```lua

    httpd = box.httpd.new(host, port[, { options } ])

```

Для запуска httpd-сервера обязательно указать хост и порт на которые данный
сервер будет забинден, а так же дополнительные опции:

* `max_header_size` (по умолчанию - 4096 байт) - максимальный размер
принимаемого заголовка HTTP-запроса;
* `header_timeout` (по умолчанию - 100 секунд) - максимальное время для
передачи заголовков клиентом (если клиент после коннекта за это время не
передаст полный заголовок, то связь с ним будет принудительно разорвана);
* `templates` (по умолчанию - '.' (рабочая директория тарантула) - путь
к директории с шаблонами html;
* `handler` - функция-обработчик http-запросов (если пользователь не хочет
использовать встроенную в сервер систему работы с роутами итп);

#### Низкий уровень (определение собственного `handler`)

Если пользователь определяет `handler` самостоятельно, то эта функция примет
на вход распарсенный запрос (табличка с полями):

* `proto` - версия протокола запроса
* `path` - путь запроса
* `query` - строка параметров запроса
* `method` - тип запроса
* `body` - тело запроса

Функция должна вернуть таблицу, содержающую три значения:

1. код ответа
1. хеш нормализованных заголовков (если nil, то - заголовки по умолчанию)
1. тело ответа (nil == '', таблица - конкатенируется)

```lua

	function my_handler(httpd, request)
		return {
			200,
			{ ['content-type'] = 'text/html; charset=utf8' },
			[[
				<html>
					<body>Hello, world!</body>
				</html>
			]]
		}
	end

```

### Обычное использование

В общем случае пользователь может сконфигурировать сервер на автоматическое
распределение запросов между различными обработчиками. Для этого в сервер
встроен механизм роутинга, аналогичный фреймворку
[Mojolicious](http://mojolicio.us/perldoc/Mojolicious/Guides/Routing).

Роуты можно определять:

1. непосредственно
1. используя простые подстановки
1. используя агрессивные подстановки

Примеры роутов:

```text

'/'                 -- простой роут
'/abc'              -- простой роут
'/abc/:cde'         -- роут с простой подстановкой
'/abc/:cde/:def'    -- роут с простой подстановкой
'/ghi*path'         -- роут с агрессивной подстановкой

```

конфигурирование роутинга производится при помощи вызова метода `route` у
созданного httpd сервера.

```lua

httpd
    :route({ path = '/', template = 'Hello <%= var %>' }, handle1)
    :route({ path = '/:abc/cde', file = 'users.html.el' }, handle2)
    ...

```

Каждый новый вызов route добавляет новый обработчик в список роутов
http-сервера.

При этом пользователь должен передать:

* `file` - имя файла (начиная от директории `templates`, см. конфигурирование
http-сервера в параметрах конструктора) с html-шаблоном. Если расширение
файла не указано, то автоматически подставляется `.html.el` (html with
Embedded Lua)
* `template` - тело шаблона (если шаблон уже прочитан в память)
* `path` - путь роута (примеры выше)
* `name` - имя роута

Так же пользователь передает обработчик, который будет использован для
обработки данного запроса.

#### Обработчик роута

Принимает один параметр - табличку-объект со следующими полями:

* `req` - запрос
* `resp` - объект для подготовки ответа

и методами:

* `stash(name[, value])` - доступ к переменным "захваченным" при
диспетчеризации роутов

```lua


    function hello(self)
        local id = self:stash('id')    -- сюда попадет часть :id
        local user = box.space.users:select(id)
        if user == nil then
            return self:redirect_to('/users_not_found')
        end
        return self:render({ user = user  })
    end

    httpd = box.httpd.new('127.0.0.1', 8080)
    httpd:route(
        { path = '/:id/view', template = 'Hello, <%= user.name %>' }, hello)
    httpd:start()

```

* `redirect_to` - используется для управления перенаправлениями со
страницы на страницу
* `render({})` - вызывает рендер

### Рендеринг

В шаблонах html можно использовать встроенный lua.

```html
<html>
    <head>
        <title><%= title %></title>
    </head>
    <body>
        <ul>
            % for i = 1, 10 do
                <li><%= item[i].key %>: <%= item[i].value %></li>
            % end
        </ul>
    </body>
</html>

```

Для встраивания перехода от обычного html-контента к lua и назад используются
следующие конструкции:

* `<% луа-код %>` - позволяет вставить многострочный lua код. Может
находиться в любом месте строки.
* `% луа код` - позволяет вставить однострочный lua код. Может находиться
в начале строки (пробелы и табуляции в начале строки игнорируются)

После символа `%` могут идти управляющие символы:

* `=` (например `<%= value + 1 %>`) - выполняет идущий в блоке lua код
и вставляет результат его выполнения в html. При этом производится так
же экранирование символов html (`<`, `>`, `&`, `"`)
* `==` (например `<%== value + 10 %>`) - выполняет lua код блока, вставляет
результат его выполнения в html. Экранирование символов не производится.

В шаблонах можно оперировать следующими переменными/функциями:

1. переменными, определенными в lua шаблона
1. переменными, попавшими в стеш
1. переменными, переданными в таблице функции `render`


#### Помощники

В шаблонах html можно использовать помощников - это функции, которые
определяются на стадии конфигурирования httpd сервера и доступны к
использованию во всех шаблонах.

Определить (или удалить) помощника можно следующим образом:

```lua

	-- установить помощника
	httpd:helper('time', function(self, ...) return box.time() end)
	-- сбросить помощника
	httpd:helper('some_name', nil)

```

Далее помощника можно использовать внутри html-шаблона:

```html
<div>
	Current timestamp: <%= time() %>
</div>
```

Первым параметром хелперу всегда передается текущий контроллер, остальные
параметры - те что передал пользователь.


### Хуки

Для http сервера можно определить несколько обработчиков, которые
будут вызываться на разных стадиях выполнения запросов.

#### `before_routes(httpd, request)`

Вызывается перед выполнением роутинга.

В качестве параметра получает - входящий http-запрос.
Возвращаемое значение игнорируется.

Этот хук можно использовать например для добавления заголовков в request,
логгирования итп.

### `after_dispatch(cx, resp)`

Вызывается после того как обработчик запроса выполнен.

В качестве параметров получает тот же объект, что и обработчик роута,
а так же табличку со сформированным http-ответом.

Данный обработчик может модифицировать сформированный ответ.

Возвращаемое значение игнорируется.


