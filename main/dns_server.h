/*
 * dns_server.h — Mini servidor DNS para el portal cautivo:
 * responde 192.168.4.1 a cualquier nombre, así el celular abre la página
 * de configuración automáticamente al conectarse al punto de acceso.
 */
#pragma once
void dns_server_start(void);
void dns_server_stop(void);
