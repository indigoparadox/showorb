# ShowOrb

Rough daemon for displaying MQTT data on an Orbit LCD.

This can be useful when combined with [PalmOrb LCD emulator](https://palmorb.sourceforge.net/).

## Configuration

### mqtt

#### topic

There can be multiple `topic=` lines; each will feed the next token encountered while iterating through the display line under \[lcd\].

### \[lcd\]

#### display

This is a line of tokens that pull from the list of topics in the \[mqtt\] section.

 * **$T**: Does not consume a topic; shows the current time.
 * **$F**: Shows the next topic as a raw floating-point number.
 * **$D**: Shows the next topic as a raw integer.
 * **$W**: Shows an icon from the \[weather\] section with the following key for interpreting the number fetched from the corresponding topic: 0 for rain, 1 for clouds, 2 for moon, 3 for sun.
 * **$N**: Does not consume a topic; inserts a newline character.

### \[fonts\]

#### icon

This is a list of special bitmaps to send to the LCD for special icons, mainly used by the **$W** token in the `display=` line in the \[lcd\] section right now..

Each line should be in the format of `icon=/path/to/icon.bmp`

### \[weather\]

This is a list of icon indexes, mainly used by the **$W** token in the `display=` line in the \[lcd\] section right now..

The bitmaps to send to the LCD for these special icon characters are listed at the **index** position in the \[fonts\] section. e.g. sun\_icon=1 will tell the LCD that the "sun" icon should be the special font character loaded by the first `icon=` line in the \[fonts\] section.

 * sun\_icon
 * cloud\_icon
 * rain\_icon
 * moon\_icon

