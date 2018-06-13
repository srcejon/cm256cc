/*
    Copyright (c) 2015 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of CM256 nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <sys/time.h>

#include "../cm256.h"

long long getUSecs()
{
    struct timeval tp;
    gettimeofday(&tp, 0);
    return (long long) tp.tv_sec * 1000000L + tp.tv_usec;
}

void initializeBlocks(CM256::cm256_block originals[256], int blockCount, int blockBytes)
{
    for (int i = 0; i < blockCount; ++i)
    {
        for (int j = 0; j < blockBytes; ++j)
        {
            const uint8_t expected = (uint8_t)(i + j * 13);
            uint8_t* data = (uint8_t*)originals[i].Block;
            data[j] = expected;
        }
    }
}

bool validateSolution(CM256::cm256_block_t* blocks, int blockCount, int blockBytes)
{
    uint8_t seen[256] = { 0 };

    for (int i = 0; i < blockCount; ++i)
    {
        uint8_t index = blocks[i].Index;

        if (index >= blockCount)
        {
            return false;
        }

        if (seen[index])
        {
            return false;
        }

        seen[index] = 1;

        for (int j = 0; j < blockBytes; ++j)
        {
            const uint8_t expected = (uint8_t)(index + j * 13);
            uint8_t* blockData = (uint8_t*)blocks[i].Block;
            if (blockData[j] != expected)
            {
                return false;
            }
        }
    }

    return true;
}



bool ExampleFileUsage()
{
    CM256 cm256;

    if (!cm256.isInitialized())
    {
        return false;
    }

    CM256::cm256_encoder_params params;

    // Number of bytes per file block
    params.BlockBytes = 1296;

    // Number of blocks
    params.OriginalCount = 100;

    // Number of additional recovery blocks generated by encoder
    params.RecoveryCount = 30;

    // Size of the original file
    static const int OriginalFileBytes = params.OriginalCount * params.BlockBytes;

    // Allocate and fill the original file data
    uint8_t* originalFileData = new uint8_t[OriginalFileBytes];
    for (int i = 0; i < OriginalFileBytes; ++i)
    {
        originalFileData[i] = (uint8_t)i;
    }

    // Pointers to data
    CM256::cm256_block blocks[256];
    for (int i = 0; i < params.OriginalCount; ++i)
    {
        blocks[i].Block = originalFileData + i * params.BlockBytes;
    }

    // Recovery data
    uint8_t* recoveryBlocks = new uint8_t[params.RecoveryCount * params.BlockBytes];

    // Generate recovery data
    if (cm256.cm256_encode(params, blocks, recoveryBlocks))
    {
        delete[] originalFileData;
        delete[] recoveryBlocks;
        return false;
    }

    // Initialize the indices
    for (int i = 0; i < params.OriginalCount; ++i)
    {
        blocks[i].Index = CM256::cm256_get_original_block_index(params, i);
    }

    //// Simulate loss of data, substituting a recovery block in its place ////
    for (int i = 0; i < params.RecoveryCount && i < params.OriginalCount; ++i)
    {
        blocks[i].Block = recoveryBlocks + params.BlockBytes * i; // First recovery block
        blocks[i].Index = CM256::cm256_get_recovery_block_index(params, i); // First recovery block index
    }
    //// Simulate loss of data, substituting a recovery block in its place ////

    if (cm256.cm256_decode(params, blocks))
    {
        delete[] originalFileData;
        delete[] recoveryBlocks;
        return false;
    }

    for (int i = 0; i < params.RecoveryCount && i < params.OriginalCount; ++i)
    {
        uint8_t* block = (uint8_t*)blocks[i].Block;
        int index = blocks[i].Index;

        for (int j = 0; j < params.BlockBytes; ++j)
        {
            const uint8_t expected = (uint8_t)(j + index * params.BlockBytes);
            if (block[j] != expected)
            {
                delete[] originalFileData;
                delete[] recoveryBlocks;
                return false;
            }
        }
    }

    delete[] originalFileData;
    delete[] recoveryBlocks;

    return true;
}

bool example2()
{
    static const int payloadSize = 256; // represents 4 subframes of 64 bytes
#pragma pack(push, 1)
    struct ProtectedBlock
    {
        uint8_t blockIndex;
        uint8_t data[payloadSize];
    };
    struct SuperBlock
    {
        uint8_t        frameIndex;
        uint8_t        blockIndex;
        ProtectedBlock protectedBlock;
    };
#pragma pack(pop)

    CM256 cm256;
    if (!cm256.isInitialized())
    {
        return false;
    }

    CM256::cm256_encoder_params params;

    // Number of bytes per file block
    params.BlockBytes = sizeof(ProtectedBlock);

    // Number of blocks
    params.OriginalCount = 128;  // Superframe = set of protected frames

    // Number of additional recovery blocks generated by encoder
    params.RecoveryCount = 32;

    SuperBlock* txBuffer = new SuperBlock[params.OriginalCount+params.RecoveryCount];
    ProtectedBlock* txRecovery = new ProtectedBlock[params.RecoveryCount];
    CM256::cm256_block *txDescriptorBlocks = new CM256::cm256_block[params.OriginalCount+params.RecoveryCount];
    int frameCount = 0;

    // Fill original data
    for (int i = 0; i < params.OriginalCount+params.RecoveryCount; ++i)
    {
        txBuffer[i].frameIndex = frameCount;
        txBuffer[i].blockIndex = i;

        if (i < params.OriginalCount)
        {
            txBuffer[i].protectedBlock.blockIndex = i;

            for (int j = 0; j < payloadSize; ++j)
            {
                txBuffer[i].protectedBlock.data[j] = i;
            }

            txDescriptorBlocks[i].Block = (void *) &txBuffer[i].protectedBlock;
            txDescriptorBlocks[i].Index = txBuffer[i].blockIndex;
        }
        else
        {
            memset((void *) &txBuffer[i].protectedBlock, 0, sizeof(ProtectedBlock));
            txDescriptorBlocks[i].Block = (void *) &txBuffer[i].protectedBlock;
            txDescriptorBlocks[i].Index = i;
        }
    }

    // Generate recovery data

    long long ts = getUSecs();

    if (cm256.cm256_encode(params, txDescriptorBlocks, txRecovery))
    {
        std::cerr << "example2: encode failed" << std::endl;
        delete[] txBuffer;
        delete[] txRecovery;
        return false;
    }

    long long usecs = getUSecs() - ts;

    std::cerr << "Encoded in " << usecs << " microseconds" << std::endl;

    // insert recovery data in sent data
    for (int i = 0; i < params.RecoveryCount; i++)
    {
        txBuffer[params.OriginalCount+i].protectedBlock = txRecovery[i];
    }

    SuperBlock* rxBuffer = new SuperBlock[params.OriginalCount];
    CM256::cm256_block rxDescriptorBlocks[params.OriginalCount];
    int k = 0;

    for (int i = 0; i < params.OriginalCount+params.RecoveryCount; i++)
    {
        if (k < params.OriginalCount)
        {
            if (i % 5 != 4)
            {
                rxBuffer[k] = txBuffer[i];
                rxDescriptorBlocks[k].Block = (void *) &rxBuffer[k].protectedBlock;
                rxDescriptorBlocks[k].Index = rxBuffer[k].blockIndex;
                k++;
            }
        }
    }

    ts = getUSecs();

    if (cm256.cm256_decode(params, rxDescriptorBlocks))
    {
        delete[] txBuffer;
        delete[] txRecovery;
        delete[] rxBuffer;

        return false;
    }

    usecs = getUSecs() - ts;

    for (int i = 0; i < params.OriginalCount; i++)
    {
        std::cerr << i << ":"
                << (unsigned int) rxBuffer[i].blockIndex << ":"
                << (unsigned int) rxBuffer[i].protectedBlock.blockIndex << ":"
                << (unsigned int) rxBuffer[i].protectedBlock.data[0] << std::endl;
    }

    std::cerr << "Decoded in " << usecs << " microseconds" << std::endl;

    delete[] txDescriptorBlocks;
    delete[] txBuffer;
    delete[] txRecovery;
    delete[] rxBuffer;

    return true;
}

/**
 * This is a more realistic example with separation of received data creation (mocking) and its processing
 */
bool example3()
{
#pragma pack(push, 1)
    struct Sample
    {
        uint16_t i;
        uint16_t q;
    };
    struct Header
    {
        uint16_t frameIndex;
        uint8_t  blockIndex;
        uint8_t  filler;
    };

    static const int samplesPerBlock = (512 - sizeof(Header)) / sizeof(Sample);

    struct ProtectedBlock
    {
        Sample samples[samplesPerBlock];
    };
    struct SuperBlock
    {
        Header         header;
        ProtectedBlock protectedBlock;
    };
#pragma pack(pop)

    CM256 cm256;

    if (!cm256.isInitialized())
    {
        return false;
    }

    CM256::cm256_encoder_params params;

    // Number of bytes per file block
    params.BlockBytes = sizeof(ProtectedBlock);

    // Number of blocks
    params.OriginalCount = 128;  // Superframe = set of protected frames

    // Number of additional recovery blocks generated by encoder
    params.RecoveryCount = 32;

    SuperBlock* txBuffer = new SuperBlock[params.OriginalCount+params.RecoveryCount];
    ProtectedBlock* txRecovery = new ProtectedBlock[params.RecoveryCount];
    CM256::cm256_block txDescriptorBlocks[params.OriginalCount+params.RecoveryCount];
    int frameCount = 0;

    // Fill original data
    for (int i = 0; i < params.OriginalCount+params.RecoveryCount; ++i)
    {
        txBuffer[i].header.frameIndex = frameCount;
        txBuffer[i].header.blockIndex = i;

        if (i < params.OriginalCount)
        {
            txBuffer[i].protectedBlock.samples[0].i = i; // marker
        }
        else
        {
            memset((void *) &txBuffer[i].protectedBlock, 0, sizeof(ProtectedBlock));
        }

        txDescriptorBlocks[i].Block = (void *) &txBuffer[i].protectedBlock;
        txDescriptorBlocks[i].Index = txBuffer[i].header.blockIndex;
    }

    // Generate recovery data

    long long ts = getUSecs();

    if (cm256.cm256_encode(params, txDescriptorBlocks, txRecovery))
    {
        std::cerr << "example2: encode failed" << std::endl;
        delete[] txBuffer;
        delete[] txRecovery;
        return false;
    }

    long long usecs = getUSecs() - ts;

    std::cerr << "Encoded in " << usecs << " microseconds" << std::endl;

    // insert recovery data in sent data
    for (int i = 0; i < params.RecoveryCount; i++)
    {
        txBuffer[params.OriginalCount+i].protectedBlock = txRecovery[i];
    }

    SuperBlock* rxBuffer = new SuperBlock[params.OriginalCount + params.RecoveryCount]; // received blocks
    int k = 0;

    for (int i = 0; i < params.OriginalCount+params.RecoveryCount; i++)
    {
        if (i % 5 != 4)
        {
            rxBuffer[k] = txBuffer[i];
            k++;
        }
    }

    Sample *samplesBuffer = new Sample[samplesPerBlock * params.OriginalCount];
    ProtectedBlock* retrievedDataBuffer = (ProtectedBlock *) samplesBuffer;
    ProtectedBlock* recoveryBuffer = new ProtectedBlock[params.OriginalCount];      // recovery blocks with maximum size

    CM256::cm256_block rxDescriptorBlocks[params.OriginalCount];
    int recoveryStartIndex;
    k = 0;

    for (int i = 0; i < params.OriginalCount; i++)
    {
        int blockIndex = rxBuffer[i].header.blockIndex;

        if (blockIndex < params.OriginalCount) // it's an original block
        {
            retrievedDataBuffer[blockIndex] = rxBuffer[i].protectedBlock;
            rxDescriptorBlocks[i].Block = (void *) &retrievedDataBuffer[blockIndex];
            rxDescriptorBlocks[i].Index = blockIndex;
        }
        else // it's a recovery block
        {
            if (k == 0)
            {
                recoveryStartIndex = i;
            }

            recoveryBuffer[k] = rxBuffer[i].protectedBlock;
            rxDescriptorBlocks[i].Block = (void *) &recoveryBuffer[k];
            rxDescriptorBlocks[i].Index = blockIndex;
            k++;
        }
    }

    ts = getUSecs();

    if (cm256.cm256_decode(params, rxDescriptorBlocks))
    {
        delete[] txBuffer;
        delete[] txRecovery;
        delete[] rxBuffer;
        delete[] samplesBuffer;
        delete[] recoveryBuffer;

        return false;
    }

    usecs = getUSecs() - ts;

    for (int i = 0; i < k; i++) // recover missing blocks
    {
        int blockIndex = rxDescriptorBlocks[recoveryStartIndex+i].Index;
        retrievedDataBuffer[blockIndex] = recoveryBuffer[i];
    }

    for (int i = 0; i < params.OriginalCount; i++)
    {
        std::cerr << i << ":"
                << (unsigned int) rxDescriptorBlocks[i].Index << ":"
                << (unsigned int) retrievedDataBuffer[i].samples[0].i << std::endl;
    }

    std::cerr << "Decoded in " << usecs << " microseconds" << std::endl;

    delete[] txBuffer;
    delete[] txRecovery;
    delete[] rxBuffer;
    delete[] samplesBuffer;
    delete[] recoveryBuffer;

    return true;
}

/**
 * This is a more realistic example with separation of received data creation (mocking) and its processing
 */
bool example4()
{
#pragma pack(push, 1)
    struct Sample
    {
        uint16_t i;
        uint16_t q;
    };
    struct Header
    {
        uint16_t frameIndex;
        uint8_t  blockIndex;
        uint8_t  filler;
    };

    static const int samplesPerBlock = (512 - sizeof(Header)) / sizeof(Sample);

    struct ProtectedBlock
    {
        Sample samples[samplesPerBlock];
    };
    struct SuperBlock
    {
        Header         header;
        ProtectedBlock protectedBlock;
    };
#pragma pack(pop)

    CM256 cm256;
    if (!cm256.isInitialized())
    {
        return false;
    }

    CM256::cm256_encoder_params params;

    // Number of bytes per file block
    params.BlockBytes = sizeof(ProtectedBlock);

    // Number of blocks
    params.OriginalCount = 128;  // Superframe = set of protected frames

    // Number of additional recovery blocks generated by encoder
    params.RecoveryCount = 25;

    SuperBlock txBuffer[256];
    ProtectedBlock txRecovery[256];
    CM256::cm256_block txDescriptorBlocks[256];
    int frameCount = 0;

    // Fill original data
    for (int i = 0; i < params.OriginalCount+params.RecoveryCount; ++i)
    {
        txBuffer[i].header.frameIndex = frameCount;
        txBuffer[i].header.blockIndex = i;
        txDescriptorBlocks[i].Block = (void *) &txBuffer[i].protectedBlock;
        txDescriptorBlocks[i].Index = txBuffer[i].header.blockIndex;

        if (i < params.OriginalCount)
        {
            for (int k = 0; k < samplesPerBlock; k++)
            {
                txBuffer[i].protectedBlock.samples[k].i = rand();
                txBuffer[i].protectedBlock.samples[k].q = rand();
            }
        }
        else
        {
            memset((void *) &txBuffer[i].protectedBlock, 0, sizeof(ProtectedBlock));
        }

    }

    // Generate recovery data

    long long ts = getUSecs();

    if (cm256.cm256_encode(params, txDescriptorBlocks, txRecovery))
    {
        std::cerr << "example2: encode failed" << std::endl;
        return false;
    }

    long long usecs = getUSecs() - ts;

    std::cerr << "Encoded in " << usecs << " microseconds" << std::endl;

    // insert recovery data in sent data
    for (int i = 0; i < params.RecoveryCount; i++)
    {
        txBuffer[params.OriginalCount+i].protectedBlock = txRecovery[i];
    }

    SuperBlock* rxBuffer = new SuperBlock[256]; // received blocks
    int nbRxBlocks = 0;

    for (int i = 0; i < params.OriginalCount+params.RecoveryCount; i++)
    {
        if (i % 6 != 4)
        //if (i != 101)
        {
            rxBuffer[nbRxBlocks] = txBuffer[i];
            nbRxBlocks++;
        }
    }

    std::cerr << "exemple4: nbRxBlocks: " << nbRxBlocks << " OriginalCount: " << params.OriginalCount << std::endl;

    Sample *samplesBuffer = new Sample[samplesPerBlock * (params.OriginalCount - 1)];
    ProtectedBlock  blockZero;
    ProtectedBlock* retrievedDataBuffer = (ProtectedBlock *) samplesBuffer;
    ProtectedBlock* recoveryBuffer = new ProtectedBlock[params.OriginalCount];      // recovery blocks with maximum size

    CM256::cm256_block rxDescriptorBlocks[params.OriginalCount];
    int recoveryStartIndex = 0;
    int recoveryCount = 0;
    int nbBlocks = 0;

    for (int i = 0; i < nbRxBlocks; i++)
    {
        int blockIndex = rxBuffer[i].header.blockIndex;

        if (nbBlocks < params.OriginalCount) // not enough data store it
        {
            rxDescriptorBlocks[i].Index = blockIndex;

            if (blockIndex == 0) // it is block #0
            {
                blockZero = rxBuffer[i].protectedBlock;
                rxDescriptorBlocks[i].Block = (void *) &blockZero;
            }
            else if (blockIndex < params.OriginalCount) // it's an original block
            {
                retrievedDataBuffer[blockIndex - 1] = rxBuffer[i].protectedBlock;
                rxDescriptorBlocks[i].Block = (void *) &retrievedDataBuffer[blockIndex - 1];
            }
            else // it's a recovery block
            {
                if (recoveryCount == 0)
                {
                    recoveryStartIndex = i;
                }

                recoveryBuffer[recoveryCount] = rxBuffer[i].protectedBlock;
                rxDescriptorBlocks[i].Block = (void *) &recoveryBuffer[recoveryCount];
                recoveryCount++;
            }
        }

        nbBlocks++;

        if (nbBlocks == params.OriginalCount) // ready
        {
            if (recoveryCount > 0)
            {
                ts = getUSecs();

                if (cm256.cm256_decode(params, rxDescriptorBlocks))
                {
                    delete[] rxBuffer;
                    delete[] samplesBuffer;
                    delete[] recoveryBuffer;

                    return false;
                }

                usecs = getUSecs() - ts;
                std::cerr << "recover missing blocks..." << std::endl;

                for (int ir = 0; ir < recoveryCount; ir++) // recover missing blocks
                {
                    int blockIndex = rxDescriptorBlocks[recoveryStartIndex+ir].Index;

                    if (blockIndex == 0) // it is block #0
                    {
                        blockZero = recoveryBuffer[ir];
                    }
                    else
                    {
                        retrievedDataBuffer[blockIndex - 1] = recoveryBuffer[ir];
                    }

                    std::cerr << ir << ":" << blockIndex << ": " << recoveryBuffer[ir].samples[0].i << std::endl;
                }
            }
        }
    }

    std::cerr << "final..." << std::endl;

    for (int i = 1; i < params.OriginalCount; i++)
    {
        bool compOKi = true;
        bool compOKq = true;

        for (int k = 0; k < samplesPerBlock; k++)
        {
            if (retrievedDataBuffer[i - 1].samples[k].i != txBuffer[i].protectedBlock.samples[k].i)
            {
                std::cerr << i << ": error: " << k << ": i: " << retrievedDataBuffer[i].samples[k].i << "/" << txBuffer[i].protectedBlock.samples[k].i << std::endl;
                compOKi = false;
                break;
            }

            if (retrievedDataBuffer[i - 1].samples[k].q != txBuffer[i].protectedBlock.samples[k].q)
            {
                std::cerr << i << ": error: " << k << ": q: " << retrievedDataBuffer[i].samples[k].q << "/" << txBuffer[i].protectedBlock.samples[k].q << std::endl;
                compOKq = false;
                break;
            }
        }

        if (compOKi && compOKq)
        {
            std::cerr << i << ": OK" << std::endl;
        }
    }

    // Zero:

    bool compOKi = true;
    bool compOKq = true;

    for (int k = 0; k < samplesPerBlock; k++)
    {
        if (blockZero.samples[k].i != txBuffer[0].protectedBlock.samples[k].i)
        {
            std::cerr << "zero: error: " << k << ": i: " << blockZero.samples[k].i << "/" << txBuffer[0].protectedBlock.samples[k].i << std::endl;
            compOKi = false;
            break;
        }

        if (blockZero.samples[k].q != txBuffer[0].protectedBlock.samples[k].q)
        {
            std::cerr << "zero: error: " << k << ": q: " << blockZero.samples[k].q << "/" << txBuffer[0].protectedBlock.samples[k].q << std::endl;
            compOKq = false;
            break;
        }
    }

    if (compOKi && compOKq)
    {
        std::cerr << "zero: OK" << std::endl;
    }

    std::cerr << "Decoded in " << usecs << " microseconds" << std::endl;

    delete[] samplesBuffer;
    delete[] recoveryBuffer;

    return true;
} // example4

int main()
{
    std::cerr << "ExampleFileUsage:" << std::endl;

    if (!ExampleFileUsage())
    {
        std::cerr << "ExampleFileUsage failed" << std::endl << std::endl;
        return 1;
    }

    std::cerr << "ExampleFileUsage successful" << std::endl << std::endl;
    std::cerr << "example2:" << std::endl;


    if (!example2())
    {
        std::cerr << "example2 failed" << std::endl << std::endl;
        return 1;
    }

    std::cerr << "example2 successful" << std::endl << std::endl;
    std::cerr << "example3:" << std::endl;

    if (!example3())
    {
        std::cerr << "example3 failed" << std::endl << std::endl;
        return 1;
    }

    std::cerr << "example3 successful" << std::endl << std::endl;
    std::cerr << "example4:" << std::endl;

    if (!example4())
    {
        std::cerr << "example4 failed" << std::endl << std::endl;
        return 1;
    }

    std::cerr << "example4 successful" << std::endl;

    return 0;
}
