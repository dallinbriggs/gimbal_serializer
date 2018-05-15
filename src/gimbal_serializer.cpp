#include "gimbal_serializer.h"

namespace gimbal_serializer{


GimbalSerializer::GimbalSerializer():
    nh_(ros::NodeHandle()),
    nh_private_(ros::NodeHandle("~"))
{
    // Set/get params
    nh_private_.param<std::string>("port", port_, "/dev/gimbal");
    nh_private_.param<int>("baudrate", baudrate_, 115200);

    // Initialize serial stuff
    init_serial();

    // Setup ros subscribers and publishers
    command_sub = nh_.subscribe("gimbal/control", 1, &GimbalSerializer::command_callback, this);
    command_echo = nh_.advertise<gimbal_serializer::status>("gimbal/status", 1);
    parse_state = PARSE_STATE_IDLE;
    crc_error_count = 0;
}

void GimbalSerializer::command_callback(const geometry_msgs::Vector3StampedConstPtr &msg)
{
    x_command = msg->vector.x;
    y_command = msg->vector.y;
    z_command = msg->vector.z;
    serialize_msg();
}

void GimbalSerializer::serialize_msg()
{
    uint8_t buf[SERIAL_OUT_MSG_LENGTH];
    buf[0] = SERIAL_OUT_START_BYTE;

    memcpy(buf+1, &x_command, sizeof(float));
    memcpy(buf+5, &y_command, sizeof(float));
    memcpy(buf+9, &z_command, sizeof(float));

    uint8_t crc_value = SERIAL_CRC_INITIAL_VALUE;
    for (int i = 0; i < SERIAL_OUT_MSG_LENGTH - SERIAL_CRC_LENGTH; i++)
    {
        crc_value = out_crc8_ccitt_update(crc_value, buf[i]);
    }
    buf[SERIAL_OUT_MSG_LENGTH - 1] = crc_value;
    serial_->send_bytes(buf, SERIAL_OUT_MSG_LENGTH);
}

void GimbalSerializer::init_serial()
{
    serial_ = new async_comm::Serial(port_, (unsigned int)baudrate_);
    serial_->register_receive_callback(std::bind(&GimbalSerializer::rx_callback, this, std::placeholders::_1));

    if (!serial_->init())
    {
        std::printf("Failed to initialize serial port\n");
    }
}

uint8_t GimbalSerializer::out_crc8_ccitt_update(uint8_t outCrc, uint8_t outData)
{
    uint8_t   i;
    uint8_t   data;

    data = outCrc ^ outData;

    for ( i = 0; i < SERIAL_OUT_PAYLOAD_LENGTH; i++ )
    {
        if (( data & 0x80 ) != 0 )
        {
            data <<= 1;
            data ^= 0x07;
        }
        else
        {
            data <<= 1;
        }
    }
    return data;

}

uint8_t GimbalSerializer::in_crc8_ccitt_update(uint8_t inCrc, uint8_t inData)
{
    uint8_t   i;
    uint8_t   data;

    data = inCrc ^ inData;

    for ( i = 0; i < SERIAL_IN_PAYLOAD_LENGTH; i++ )
    {
        if (( data & 0x80 ) != 0 )
        {
            data <<= 1;
            data ^= 0x07;
        }
        else
        {
            data <<= 1;
        }
    }
    return data;

}

void GimbalSerializer::unpack_in_payload(uint8_t buf[], float *roll, float *pitch, float *yaw)
{

}

void GimbalSerializer::rx_callback(uint8_t byte)
{
    if (parse_in_byte(byte))
    {
        float roll, pitch, yaw;
        unpack_in_payload(in_payload_buf, &roll, &pitch, &yaw);
    }
}

//==================================================================
// handle an incoming byte
//==================================================================
bool GimbalSerializer::parse_in_byte(uint8_t c)
{
    bool got_message = false;
    switch (parse_state)
    {
    case PARSE_STATE_IDLE:
        if (c == SERIAL_IN_START_BYTE)
        {
            in_crc_value = SERIAL_CRC_INITIAL_VALUE;
            in_crc_value = in_crc8_ccitt_update(in_crc_value, c);

            in_payload_index = 0;
            parse_state = PARSE_STATE_GOT_START_BYTE;
        }
        break;

    case PARSE_STATE_GOT_START_BYTE:
        in_crc_value = in_crc8_ccitt_update(in_crc_value, c);
        in_payload_buf[in_payload_index++] = c;
        if (in_payload_index == SERIAL_IN_PAYLOAD_LENGTH)
        {
            parse_state = PARSE_STATE_GOT_PAYLOAD;
        }
        break;

    case PARSE_STATE_GOT_PAYLOAD:
        if (c == in_crc_value)
        {
            got_message = true;
        }
        else
        {
            crc_error_count += 1;
        }
        parse_state = PARSE_STATE_IDLE;
        break;
    }

    return got_message;
}

} //end gimbal_serializer namespace


int main(int argc, char** argv)
{
    ros::init(argc, argv, "gimbal_serial_node");
    gimbal_serializer::GimbalSerializer gimbal_serial_node;
    ros::spin();
    return 0;
}
