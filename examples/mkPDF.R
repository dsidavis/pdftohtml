
pdf("sample.pdf")
plot(1:10)
abline(v = c(2, 8), col = "red")
abline(h = c(3, 6), col = "blue")
text(5, 5, "Middle")
dev.off()

pdf("sample2.pdf")
plot(1:20, pch = 1:20)
dev.off()


