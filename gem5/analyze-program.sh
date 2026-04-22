#!/bin/bash

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
  echo "Usage: analyze-program.sh PATH-TO-DIR"
  exit 1
fi

# Thanks StackOverflow
#
# Source - https://stackoverflow.com/a/41762669
# Posted by Mark Setchell
median() {
  arr=($(printf '%d\n' "${@}" | sort -n))
  nel=${#arr[@]}
  if (( $nel % 2 == 1 )); then     # Odd number of elements
    val="${arr[ $(($nel/2)) ]}"
  else                            # Even number of elements
    (( j=nel/2 ))
    (( k=j-1 ))
    (( val=(${arr[j]} + ${arr[k]})/2 ))
  fi
  echo $val
}

all_ticks=()
all_insts=()
for statf in "$1"/*.txt; do
    all_ticks+=($(awk '$1 == "simTicks" {print $2}' "$statf"))
    all_insts+=($(awk '$1 == "simInsts" {print $2}' "$statf"))
done
 
median_insts=$(median "${all_insts[@]}")

# General Alex observations:
#
# Usually both supersecretdata and notsosecretdata have significantly higher
# ticks than other data, but supersecretdata and notsosecretdata have similar
# instructions. Finally, supersecretdata has lower average ticks than notsosecretdata
#
# So we combine this by separating out high ticks/high insts and then searching in the lower
# tick range of that space
#
high_ticks=()
for ((i=0; i<${#all_insts[@]}; i++)); do
    if [ "${all_insts[$i]}" -ge "$median_insts" ]; then
        high_ticks+=("${all_ticks[$i]}")
    fi
done
 
suspect_count=0
if [ ${#high_ticks[@]} -gt 0 ]; then
    median_high_ticks=$(median "${high_ticks[@]}")
    for ((i=0; i<${#all_insts[@]}; i++)); do
        if [ "${all_insts[$i]}" -ge "$median_insts" ] && \
           [ "${all_ticks[$i]}" -lt "$median_high_ticks" ]; then
            suspect_count=$((suspect_count + 1))
        fi
    done
fi
 
echo "$suspect_count"
 
