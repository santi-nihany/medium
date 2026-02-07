# ESTO ESTA MUY TOCADO POR IA, HACER BIEN.


# El codigo de lectura de señal funciona.



El codigo funciona correctamente, utilizando el comando `recraw <delay_us>`.
Para que lea bien la señal se debe iniciar la lectura a la vez que se emite una señal en la frecuencia específica, despues se puede parar y volver a emitir para ver la señal emitida por el aparato de principio a fin.

Con una lectura `recraw 150` puedo leer el codigo que emite mi control remoto de portón **ZAP**.

Si no estoy emitiendo una señal al iniciar una captura el modulo solo encuentra la portadora *ACTIVA* todo el tiempo.
