#!/bin/sh
if  diff -q BLOCK_STORE0 BLOCK_STORE1 > /dev/null
then
  echo "state machines for node 0 and 1 are same"
else
  echo "state machines for node 0 and 1 are different"
fi

if  diff -q BLOCK_STORE0 BLOCK_STORE2 > /dev/null
then
  echo "state machines for node 0 and 2 are same"
else
  echo "state machines for node 0 and 2 are different"
fi

if  diff -q LOG0 LOG1 > /dev/null
then
  echo "logs for node 0 and 1 are same"
else
  echo "logs for node 0 and 1 are different"
fi

if  diff -q LOG0 LOG2 > /dev/null
then
  echo "logs for node 0 and 2 are same"
else
  echo "logs for node 0 and 2 are different"
fi