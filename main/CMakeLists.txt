
idf_component_register(SRCS 
			"main.c" 
			"i2s_mic.c"
			"threadSafe_queue.c"
			"lsh_wifi.c"
			"lsh_socket.c"
			"lsh_webSocket.c"
			# "lsh_blue.c"
			INCLUDE_DIRS "."
			PRIV_REQUIRES esp_wifi
			REQUIRES esp_driver_i2s
			nvs_flash
			bt
			esp_http_server)
