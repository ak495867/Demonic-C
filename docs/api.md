# Demonic C API

## json_get

Returns a value from a JSON object by key.

### Parameters

- `json`: JSON text.
- `key`: Object key.

### Returns

The extracted value as text.

### Example

```dmc
let name: string = json_get(json_parse("{\"name\":\"Alice\"}"), "name");
```

## db_query

Executes a read query against an SQLite connection.

### Parameters

- `connection`: Database handle returned by `db_connect`.
- `sql`: SQL query text.

### Returns

Tab-separated query rows.

### Example

```dmc
let connection: int = db_connect("sqlite://mydb.db");
let rows: string = db_query(connection, "SELECT * FROM users");
```
