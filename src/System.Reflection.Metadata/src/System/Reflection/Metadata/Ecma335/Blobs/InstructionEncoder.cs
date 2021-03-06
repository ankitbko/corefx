// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;

#if !SRM
using Microsoft.CodeAnalysis.CodeGen;
#endif

#if SRM
namespace System.Reflection.Metadata.Ecma335.Blobs
#else
namespace Roslyn.Reflection.Metadata.Ecma335.Blobs
#endif
{
#if SRM
    public
#endif
    struct InstructionEncoder
    {
        public BlobBuilder Builder { get; }
        private readonly int _startPosition;

        public InstructionEncoder(BlobBuilder builder)
        {
            Builder = builder;
            _startPosition = builder.Count;
        }

        public int Offset => Builder.Count - _startPosition;

        public void OpCode(ILOpCode code)
        {
            if (unchecked((byte)code) == (ushort)code)
            {
                Builder.WriteByte((byte)code);
            }
            else
            {
                // IL opcodes that occupy two bytes are written to
                // the byte stream with the high-order byte first,
                // in contrast to the little-endian format of the
                // numeric arguments and tokens.
                Builder.WriteUInt16BE((ushort)code);
            }
        }

        public void Token(EntityHandle handle)
        {
            Token(MetadataTokens.GetToken(handle));
        }

        public void Token(int token)
        {
            Builder.WriteInt32(token);
        }

        public void LongBranchTarget(int ilOffset)
        {
            Builder.WriteInt32(ilOffset);
        }

        public void ShortBranchTarget(byte ilOffset)
        {
            Builder.WriteByte(ilOffset);
        }

        public void LoadString(int token)
        {
            OpCode(ILOpCode.Ldstr);
            Token(token);
        }

        public void Call(EntityHandle methodHandle)
        {
            OpCode(ILOpCode.Call);
            Token(methodHandle);
        }

        public void CallIndirect(StandaloneSignatureHandle signature)
        {
            OpCode(ILOpCode.Calli);
            Token(signature);
        }

        public void LoadConstantI4(int value)
        {
            ILOpCode code;
            switch (value)
            {
                case -1: code = ILOpCode.Ldc_i4_m1; break;
                case 0: code = ILOpCode.Ldc_i4_0; break;
                case 1: code = ILOpCode.Ldc_i4_1; break;
                case 2: code = ILOpCode.Ldc_i4_2; break;
                case 3: code = ILOpCode.Ldc_i4_3; break;
                case 4: code = ILOpCode.Ldc_i4_4; break;
                case 5: code = ILOpCode.Ldc_i4_5; break;
                case 6: code = ILOpCode.Ldc_i4_6; break;
                case 7: code = ILOpCode.Ldc_i4_7; break;
                case 8: code = ILOpCode.Ldc_i4_8; break;

                default:
                    if (unchecked((sbyte)value == value))
                    {
                        OpCode(ILOpCode.Ldc_i4_s);
                        Builder.WriteSByte(unchecked((sbyte)value));
                    }
                    else
                    {
                        OpCode(ILOpCode.Ldc_i4);
                        Builder.WriteInt32(value);
                    }

                    return;
            }

            OpCode(code);
        }

        public void LoadConstantI8(long value)
        {
            OpCode(ILOpCode.Ldc_i8);
            Builder.WriteInt64(value);
        }

        public void LoadConstantR4(float value)
        {
            OpCode(ILOpCode.Ldc_r4);
            Builder.WriteSingle(value);
        }

        public void LoadConstantR8(double value)
        {
            OpCode(ILOpCode.Ldc_r8);
            Builder.WriteDouble(value);
        }

        public void LoadLocal(int slotIndex)
        {
            switch (slotIndex)
            {
                case 0: OpCode(ILOpCode.Ldloc_0); break;
                case 1: OpCode(ILOpCode.Ldloc_1); break;
                case 2: OpCode(ILOpCode.Ldloc_2); break;
                case 3: OpCode(ILOpCode.Ldloc_3); break;
                default:
                    if (slotIndex < 0xFF)
                    {
                        OpCode(ILOpCode.Ldloc_s);
                        Builder.WriteByte(unchecked((byte)slotIndex));
                    }
                    else
                    {
                        OpCode(ILOpCode.Ldloc);
                        Builder.WriteInt32(slotIndex);
                    }
                    break;
            }
        }

        public void StoreLocal(int slotIndex)
        {
            switch (slotIndex)
            {
                case 0: OpCode(ILOpCode.Stloc_0); break;
                case 1: OpCode(ILOpCode.Stloc_1); break;
                case 2: OpCode(ILOpCode.Stloc_2); break;
                case 3: OpCode(ILOpCode.Stloc_3); break;
                default:
                    if (slotIndex < 0xFF)
                    {
                        OpCode(ILOpCode.Stloc_s);
                        Builder.WriteByte(unchecked((byte)slotIndex));
                    }
                    else
                    {
                        OpCode(ILOpCode.Stloc);
                        Builder.WriteInt32(slotIndex);
                    }
                    break;
            }
        }

        public void LoadLocalAddress(int slotIndex)
        {
            if (slotIndex < 0xFF)
            {
                OpCode(ILOpCode.Ldloca_s);
                Builder.WriteByte(unchecked((byte)slotIndex));
            }
            else
            {
                OpCode(ILOpCode.Ldloca);
                Builder.WriteInt32(slotIndex);
            }
        }

        public void LoadArgument(int argumentIndex)
        {
            switch (argumentIndex)
            {
                case 0: OpCode(ILOpCode.Ldarg_0); break;
                case 1: OpCode(ILOpCode.Ldarg_1); break;
                case 2: OpCode(ILOpCode.Ldarg_2); break;
                case 3: OpCode(ILOpCode.Ldarg_3); break;
                default:
                    if (argumentIndex < 0xFF)
                    {
                        OpCode(ILOpCode.Ldarg_s);
                        Builder.WriteByte(unchecked((byte)argumentIndex));
                    }
                    else
                    {
                        OpCode(ILOpCode.Ldarg);
                        Builder.WriteInt32(argumentIndex);
                    }
                    break;
            }
        }

        public void LoadArgumentAddress(int argumentIndex)
        {
            if (argumentIndex < 0xFF)
            {
                OpCode(ILOpCode.Ldarga_s);
                Builder.WriteByte(unchecked((byte)argumentIndex));
            }
            else
            {
                OpCode(ILOpCode.Ldarga);
                Builder.WriteInt32(argumentIndex);
            }
        }

        public void StoreArgument(int argumentIndex)
        {
            if (argumentIndex < 0xFF)
            {
                OpCode(ILOpCode.Starg_s);
                Builder.WriteByte(unchecked((byte)argumentIndex));
            }
            else
            {
                OpCode(ILOpCode.Starg);
                Builder.WriteInt32(argumentIndex);
            }
        }
    }
}
