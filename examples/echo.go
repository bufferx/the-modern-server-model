package main

import (
	"log"
	"net"
)

func bindSocket() (net.Listener, error) {
	listener, err := net.Listen("tcp", ":12347")
	if err != nil {
		log.Println(err)
		return nil, err
	}

	return listener, nil
}

func echo(conn net.Conn) {
	buffer := make([]byte, 1024)

	for {
		readSize, err := conn.Read(buffer)
		if err != nil {
			log.Println(err)
			return
		}

		conn.Write(buffer[:readSize])
	}
}

func eventDispatch(listener net.Listener) {
	conn, err := listener.Accept()
	if err != nil {
		log.Println(err)
		return
	}

	go echo(conn)
}

func eventLoop(listener net.Listener) {
	for {
		eventDispatch(listener)
	}
}

func main() {
	listener, _ := bindSocket()

	eventLoop(listener)
}
