# kshook

Network forwarder for SOUND VOLTEX EXCEED GEAR コナステ

## Usage

- Ensure at least one `.kshook.json` service file exists.
- Load `kshook.dll` into the game process.

### Services

Service files contain a URL and an optional token which is used in the `Authorization` header of requests.

```json
{
  "url": "https://djtrackers.local/v1/kshook",
  "token": "d41d8cd98f00b204e9800998ecf8427e "
}
```

### Debug mode

Renaming the library to `kshook_debug.dll` will enable additional logging messages.