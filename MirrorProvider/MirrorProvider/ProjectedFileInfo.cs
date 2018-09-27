namespace MirrorProvider
{
    public class ProjectedFileInfo
    {
        public ProjectedFileInfo(string name, long size, Type type)
        {
            this.Name = name;
            this.Size = size;
            this.Type = type;
        }

        public enum Type
        {
            Invalid,

            File,
            Directory,
            SymLink

        }

        public string Name { get; }
        public long Size { get; }
        public Type Type { get; }
    }
}
