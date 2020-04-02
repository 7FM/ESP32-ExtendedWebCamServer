#!/bin/bash
for file in `ls *.html`; do
    echo "Minify: $file" && \
    cp "$file" "copy_$file" && \
    sed -i 's:^//.*$::g' "$file" && \
    sed -i -r 's:\s+//.*$::g' "$file" && \
    sed -i -e 's/  //g' "$file" && \
    sh -c "tr --delete '\n' < \"$file\" > mini_\"$file\"" && \
    sh -c "tr --delete '\r' < mini_\"$file\" > \"$file\"" && \
    rm mini_"$file" && \
    echo "Compressing: $file" && \
    gzip -f "$file" && \
    mv "copy_$file" "$file"
done
