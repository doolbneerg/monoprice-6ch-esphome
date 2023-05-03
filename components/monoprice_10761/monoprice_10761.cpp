/* Copyright (C) 2021 Johan Henkens
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "monoprice_10761.h"

#include "esphome/core/log.h"
#include "esphome/core/util.h"

static const char *TAG = "monoprice_10761";
static const int BUF_SIZE = 128;

using namespace esphome;
using namespace monoprice_10761;

void Monoprice10761::setup()
{
    this->serial_read_buf_.reserve(BUF_SIZE);

    this->update();
}

void Monoprice10761::loop()
{
    this->read_from_rs232();
}

void Monoprice10761::update()
{
    uint8_t command[5] = {'?', '1', '0', '\r', '\n'};
    for (uint8_t i = 0; i < this->zone_count_ / 6; i++)
    {
        ESP_LOGD(TAG, "Updating expansion %u...", i);
        command[1] = '1' + i;
        this->write_command(command, (uint8_t)5);
    }
}

ZoneStatus *Monoprice10761::get_zone(uint8_t zone_id)
{
    uint8_t index = ZONE_ID_TO_INDEX(zone_id);
    if (index >= this->zone_count_)
    {
        ESP_LOGE(TAG, "Index out of range for zone_id %u", zone_id);
        return this->zones_[0];
    }
    return this->zones_[index];
}

void Monoprice10761::read_from_rs232()
{
    int len;
    while ((len = this->uart_->available()) > 0)
    {
        uint8_t buf[len];
        this->uart_->read_array(buf, len);
        for (size_t i = 0; i < len; i++)
        {
            this->serial_read_buf_.push_back(buf[i]);
            if (buf[i] == '#')
            { // If we finished a line, flush it
                size_t line_len = this->serial_read_buf_.size();
                if (line_len == 1)
                    continue;

                // // Debug info
                // char debug_buf[line_len-1];
                // memcpy(debug_buf, this->serial_read_buf_.data(), line_len-1);
                // debug_buf[line_len] = 0;
                // ESP_LOGD(TAG, "READ: %s", debug_buf);

                this->parse_command(this->serial_read_buf_.data(), line_len);

                this->serial_read_buf_.clear();
            }
        }
    }
}

char *get_command_as_string(const char *buf_input, size_t len_input)
{
    char *buf = (char *)buf_input;
    size_t len = len_input;
    while (len > 0 &&
           (buf[0] == '#' ||
            buf[0] == '\r' ||
            buf[0] == '\n' ||
            buf[0] == ' '))
    {
        buf++;
        len--;
    }

    while (len > 0 &&
           (buf[len - 1] == '#' ||
            buf[len - 1] == '\r' ||
            buf[len - 1] == '\n' ||
            buf[len - 1] == ' '))
    {
        len--;
    }

    char *result = new char[len + 1];
    memcpy(result, buf, len);
    result[len] = 0;
    return result;
}

void Monoprice10761::parse_command(const char *buf_input, size_t len_input)
{
    char *c_str = get_command_as_string(buf_input, len_input);
    size_t len = strlen(c_str);

    // ESP_LOGI(TAG,"Read: %s", c_str);

    if (c_str[0] == '>' || c_str[0] == '<')
    {
        char _1 = c_str[1];
        char _2 = c_str[2];
        unsigned char zone = ((_1 - '0') * 10) + (_2 - '0');
        ESP_LOGI(TAG, "Found zone status for %i", zone);
        unsigned char zoneIndex = ((_1 - '1') * 6) + (_2 - '1');
        if (this->zone_count_ <= zoneIndex)
        {
            ESP_LOGD(TAG, "Zone %i is out of range of configured zone count %i", zone, this->zone_count_);
        }
        else
        {
            ZoneStatus *status = this->zones_[zoneIndex];
            if (c_str[0] == '>')
            {
                status->update(c_str);
            }
            else
            {
                ZoneStatusDataType type = ZoneStatus::str_to_data_type(c_str + 3);
                if (type == ZoneStatusDataType::UNKNOWN)
                {
                    ESP_LOGE(TAG, "Unknown attribute: %c%c", c_str[3], c_str[4]);
                }
                else
                {
                    ESP_LOGD(TAG, "Updating %c%c", c_str[3], c_str[4]);
                    uint8_t data = TO_UINT8T(c_str, 5);
                    status->update(type, data);
                }
            }
            status->dump();
        }
    }

    if (strcmp(c_str, "Command Error.") == 0)
    {
        ESP_LOGE(TAG, "Command error received");
        this->errors_++;
    }

    delete c_str;
}

void Monoprice10761::write_command(const uint8_t *cmd, size_t len)
{
    this->uart_->write_array(cmd, len);
    this->read_from_rs232();
}

void Monoprice10761::dump_config()
{
    this->check_uart_settings(9600);
}

void ZoneStatus::update(char *zoneStatus)
{
    const unsigned char base = 3;
    this->update(ZoneStatusDataType::PublicAnnouncement, TO_BOOL(zoneStatus, base + 0));
    this->update(ZoneStatusDataType::Power, TO_BOOL(zoneStatus, base + 2));
    this->update(ZoneStatusDataType::Mute, TO_BOOL(zoneStatus, base + 4));
    this->update(ZoneStatusDataType::DoNotDisturb, TO_BOOL(zoneStatus, base + 6));
    this->update(ZoneStatusDataType::Volume, TO_UINT8T(zoneStatus, base + 8));
    this->update(ZoneStatusDataType::Trebel, TO_UINT8T(zoneStatus, base + 10));
    this->update(ZoneStatusDataType::Bass, TO_UINT8T(zoneStatus, base + 12));
    this->update(ZoneStatusDataType::Balance, TO_UINT8T(zoneStatus, base + 14));
    this->update(ZoneStatusDataType::Channel, TO_UINT8T(zoneStatus, base + 16));
    this->update(ZoneStatusDataType::KeypadStatus, TO_BOOL(zoneStatus, base + 18));
}

void ZoneStatus::update(ZoneStatusDataType type, uint8_t val)
{
    const uint8_t type_uint8 = (uint8_t)type;
    if (this->data_[type_uint8] != val)
    {
        this->data_[type_uint8] = val;
        // Run through listeners
        for (auto &listener : this->listeners_)
        {
            if (listener.data_type == type)
            {
                listener.on_update(val);
            }
        }
    }
}

void ZoneStatus::dump()
{
    ESP_LOGD(TAG, "ZoneStatus(%u): pa(%u) pr(%u) mu(%u) dt(%u) vo(%u) tr(%u) bs(%u) bl(%u) ch(%u) ls(%u)",
             this->zone_,
             this->data_[(uint8_t)ZoneStatusDataType::PublicAnnouncement],
             this->data_[(uint8_t)ZoneStatusDataType::Power],
             this->data_[(uint8_t)ZoneStatusDataType::Mute],
             this->data_[(uint8_t)ZoneStatusDataType::DoNotDisturb],
             this->data_[(uint8_t)ZoneStatusDataType::Volume],
             this->data_[(uint8_t)ZoneStatusDataType::Trebel],
             this->data_[(uint8_t)ZoneStatusDataType::Bass],
             this->data_[(uint8_t)ZoneStatusDataType::Balance],
             this->data_[(uint8_t)ZoneStatusDataType::Channel],
             this->data_[(uint8_t)ZoneStatusDataType::KeypadStatus]);
}

void ZoneStatus::set(ZoneStatusDataType type, const unsigned char val)
{
    const int len = 9;
    uint8_t buf[len];
    buf[0] = '<';
    buf[1] = '0' + this->zone_ / 10;
    buf[2] = '0' + this->zone_ % 10;
    const char *type_str = ZoneStatus::data_type_to_str(type);
    buf[3] = type_str[0];
    buf[4] = type_str[1];
    buf[5] = '0' + val / 10;
    buf[6] = '0' + val % 10;
    buf[7] = '\r';
    buf[8] = '\n';
    ESP_LOGD(TAG, "Setting zone %u param %s to %u", this->zone_, type_str, val);
    this->send_command_(buf, len);
}

void ZoneStatus::register_listener(ZoneStatusDataType data_type, const std::function<void(uint8_t)> &func)
{
    auto listener = ZoneStatusListener{
        .data_type = data_type,
        .on_update = func,
    };
    this->listeners_.push_back(listener);
}
