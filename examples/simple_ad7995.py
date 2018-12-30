import logging
import sys
from typing import List, Optional

# Note: this requires that you install the companion Python module
#  -> https://github.com/coddingtonbear/python-saleae-enrichable-analyzer
# Additionally, that repository includes a more-advanced handler for this
# chip type.  If you intend to this example as more than an example, you
# may have better results using the enrichment script bundled in the
# above repository!
from saleae_enrichable_analyzer import (
    Channel, EnrichableAnalyzer, Marker, MarkerType
)


logger = logging.getLogger(__name__)


class AD7995Analyzer(EnrichableAnalyzer):
    ADDRESS = 0b0101000

    def __init__(self, *args, **kwargs):
        super(AD7995Analyzer, self).__init__(*args, **kwargs)

        self._packets = {}

    def store_frame(
        self,
        packet_id,
        frame_index,
        frame_type,
        flags,
        value,
    ):
        if packet_id not in self._packets:
            self._packets[packet_id] = []

        if not any(
            f['frame_index'] == frame_index for f in self._packets[packet_id]
        ):
            self._packets[packet_id].append({
                'frame_index': frame_index,
                'frame_type': frame_type,
                'flags': flags,
                'value': value,
            })

    def get_packet_frames(self, packet_id):
        return sorted(
            self._packets.get(packet_id, []),
            key=lambda f: f['frame_index'],
        )

    def get_packet_length(self, packet_id):
        return len(self._packets[packet_id])

    def get_packet_frame_index(self, packet_id, frame_index):
        return list(
            map(
                lambda frame: frame['frame_index'],
                self.get_packet_frames(packet_id)
            )
        ).index(frame_index)

    def get_packet_nth_frame(self, packet_id, idx):
        return self.get_packet_frames(packet_id)[idx]

    def handle_marker(
        self,
        packet_id: Optional[int],
        frame_index: int,
        sample_count: int,
        start_sample: int,
        end_sample: int,
        frame_type: int,
        flags: int,
        value1: int,   # SPI: MOSI; I2C: SDA
        value2: int,   # SPI: MISO; I2C: Undefined
    ):
        # Data is spread across up to three frames; we need to
        # gather data across multiple frames to display meaningful data
        self.store_frame(
            packet_id,
            frame_index,
            frame_type,
            flags,
            value1,
        )

        return []

    def handle_bubble(
        self,
        packet_id: Optional[int],
        frame_index: int,
        start_sample: int,
        end_sample: int,
        frame_type: int,
        flags: int,
        direction: Channel,
        value: int
    ) -> List[str]:
        try:
            address_frame = self.get_packet_nth_frame(packet_id, 0)
        except IndexError:
            logger.error(
                "Could not find address packet for packet %s",
                hex(packet_id)
            )
            return []

        if(address_frame['value'] >> 1 != self.ADDRESS):
            # This isn't our device; don't return anything!
            return []

        is_read = address_frame['value'] & 0b1
        is_write = not is_read
        if (
            (
                not self.get_packet_length(packet_id) == 2
                and is_write
            )
            or
            (
                not self.get_packet_length(packet_id) == 3
                and is_read
            )
        ):
            # We don't have quite enough data to do anything
            return [
                '{write} {length}'.format(
                    write='W' if is_write else 'R',
                    length=self.get_packet_length(packet_id)
                )
            ]

        frame_index = self.get_packet_frame_index(packet_id, frame_index)

        if is_write:
            if frame_index == 0:
                return [
                    "Write to ADC Configuration",
                    "W to ADC",
                    "W",
                ]
            elif frame_index == 1:
                ch3 = value & (1 << 7)
                ch2 = value & (1 << 6)
                ch1 = value & (1 << 5)
                ch0 = value & (1 << 4)

                channels = {
                    '0': ch0,
                    '1': ch1,
                    '2': ch2,
                    '3': ch3,
                }
                channels_enabled = [
                    name for name, en in channels.items() if en
                ]

                ref_sel = value & (1 << 3)
                fltr = value & (1 << 2)
                bit_trial = value & (1 << 1)
                sample = value & (1 << 0)

                features = {
                    ('External Reference', 'Ext Ref'): ref_sel,
                    ('SDA and SCL Filtering', 'Filter'): not fltr,
                    ('Bit Trial Delay', 'Bit Trial'): not bit_trial,
                    ('Sample Delay', 'Samp. Del.'): not sample,
                }
                features_enabled = [
                    names for names, en in features.items() if en
                ]

                return [
                    (
                        'Channels: {channels}; Features: {features}'.format(
                            channels=', '.join(channels_enabled),
                            features=', '.join(f[0] for f in features_enabled),
                        )
                    ),
                    (
                        'Ch: {channels}; Feat: {features}'.format(
                            channels='/'.join(channels_enabled),
                            features='/'.join(f[1] for f in features_enabled),
                        )
                    ),
                    (
                        'Ch: {channels}; Feat: {features}'.format(
                            channels='/'.join(channels_enabled),
                            features=bin(value & 0b1111)
                        )
                    ),
                    (
                        'Ch: {channels}'.format(
                            channels='/'.join(channels_enabled),
                        )
                    ),
                    (
                        '{channels}'.format(
                            channels='/'.join(channels_enabled),
                        )
                    ),
                    bin(value)
                ]
        else:
            if frame_index == 0:
                return [
                    "Read ADC Value",
                    "R from ADC",
                    "R",
                ]
            elif frame_index == 1:
                channel = (value >> 4) & 0b11

                return [
                    "Channel: {ch}".format(ch=channel),
                    "Ch: {ch}".format(ch=channel),
                    channel,
                ]
            elif frame_index == 2:
                address_frame = self.get_packet_nth_frame(packet_id, 1)
                msb = (address_frame['value'] << 6)
                lsb = value >> 2

                return [
                    msb + lsb
                ]

        return [
            bin(value)
        ]


if __name__ == '__main__':
    AD7995Analyzer.run(sys.argv[1:])
