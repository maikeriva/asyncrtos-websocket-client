# Get started with this example

Add AsyncRTOS, AsyncRTOS WiFi, and AsyncRTOS Websocket as components to the project, for example using git submodules:

```
git submodule add git@github.com:maikeriva/asyncrtos.git ./components/asyncrtos
git submodule add git@github.com:maikeriva/asyncrtos-wifi-client.git ./components/asyncrtos-wifi-client
git submodule add git@github.com:maikeriva/asyncrtos-websocket-client.git ./components/asyncrtos-websocket-client
```

Then simply build, flash, and monitor the project with IDF.

```
idf.py build flash monitor
```
