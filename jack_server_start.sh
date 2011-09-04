echo "   * Starting Jack Server..."
#jackd -d alsa &
#jackd --realtime --port-max 32 -d alsa -d hw:0 -S -i1 -o2 -r16000 -p128 -n2 &
jackd --realtime --port-max 32 -d alsa -S -i1 -o2 -r16000 -p128 -n2 

sleep 2
echo "   * Adding local Jack client 1... "
jack_load -i "-d hw:0 -i1 -o2 -r16000 -p128 -n2" dev1 audioadapter &

#sleep 2
#echo "   * Adding local Jack client 2... "
#jack_load -i "-d hw:1 -i1 -o2 -r16000 -p128 -n2" dev2 audioadapter
#jack_netsource -H 10.0.14.63 &

#sleep 2
#echo "   * Adding local Jack client 3... "
#jack_load -i "-d hw:2 -i1 -o2 -r16000 -p128 -n2" dev3 audioadapter #> /dev/null 2>&1

#sleep 2
#echo "   * Adding local Jack client 4... "
#jack_load -i "-d hw:3 -i1 -o2 -r16000 -p128 -n2" dev4 audioadapter

qjackctl

