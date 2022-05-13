#!/bin/bash

for i in {0..10}
do
	./client &
done

echo "Run Scalable Clients!"